// abc802/emu/src/main.c - bin/abc802, the Luxor ABC802 machine target.
//
// A second Z80 machine alongside abc80/ and cpm/, sharing the same proven
// core (z80core/z80.o + alu.o) via z80_execute(). See
// abc802/docs/ABC802_ROADMAP.md for status and abc802/docs/ABC802_REFERENCE.md
// for the hardware being modeled.

#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "../../../z80core/z80.h"
#include "memory.h"
#include "ports.h"
#include "render.h"

#define DEFAULT_ROM_DIR "abc802/resources/rom"
#define DEFAULT_DOS_ROM "ABC802-dos.32-31.bin"

// Real ABC802 Z80 clock: 3 MHz, derived as X01/4 from the 12 MHz crystal
// (ABC802_REFERENCE.md's CPU and clock section). Needed to convert
// T-states into real seconds for --interactive's pacing.
#define ABC802_CLOCK_HZ 3000000.0

// --interactive real-time pacing/rendering, on the same terms
// abc80/emu/src/main.c already established for its own --interactive
// mode. Batch (default) mode runs z80_execute() as fast as the host can
// go, which is right for a one-shot debug run but wrong for a live
// session: the default 20,000,000 T-state cap alone completes in well
// under a second of wall-clock time, ending the "interactive" session
// before a human could react. --interactive instead throttles execution
// to real ABC802 speed and removes the fixed cap (see main()'s own
// argument handling), checked every ABC802_PACING_CHECK_INTERVAL
// instructions rather than after every single one - clock_gettime() and
// nanosleep() are real syscalls, and three million of them a second
// would swamp the actual emulation work.
#define ABC802_PACING_CHECK_INTERVAL 500
#define ABC802_RENDER_INTERVAL_SEC (1.0 / 30.0)

// Mirrors abc80/emu/src/main.c's abc80_console_init()/_shutdown() and,
// behind it, cpm/emu/src/cpm.c's - the same real terminal-mode problem
// (character-at-a-time input, no host-side echo since the ROM does its
// own through character RAM, real Enter arriving as a genuine 0x0D
// rather than being silently rewritten to 0x0A, Ctrl-S not being
// swallowed as XOFF) applies here identically, so the fix is the same.
// Deliberately duplicated rather than shared: this is ABC802-only code,
// and the other two targets' console glue is theirs by design (see
// CLAUDE.md on why each machine target's own glue stays separate).
//
// VINTR is disabled via _POSIX_VDISABLE so a real Ctrl-C keystroke
// arrives as a plain 0x03 byte and reaches the ROM's own BASIC, which is
// where a break belongs - the same choice the ABC80 target made, and for
// the same reason. ISIG stays enabled, so Ctrl-\ (SIGQUIT) is still this
// tool's own quit key.
static struct termios abc802_orig_termios;
static int abc802_termios_saved = 0;

static void abc802_console_shutdown(void) {
    if (abc802_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &abc802_orig_termios);
    }
}

static void abc802_console_init(void) {
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &abc802_orig_termios) != 0) return;
    abc802_termios_saved = 1;
    atexit(abc802_console_shutdown);

    struct termios raw = abc802_orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);          // no line buffering, no local echo
    raw.c_iflag &= ~(ICRNL | INLCR | IGNCR);  // real Enter must arrive as 0x0D
    raw.c_iflag &= ~IXON;                     // don't swallow Ctrl-S/Ctrl-Q
    raw.c_cc[VINTR] = _POSIX_VDISABLE;        // Ctrl-C reaches the ROM instead
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

// Set by the signal handler, checked by the main loop so it can exit
// through the normal end-of-main path - the run summary and, critically,
// the atexit-registered terminal restore above. SIGINT is handled too,
// not just SIGQUIT: disabling VINTR only stops the terminal driver from
// raising it via Ctrl-C, but an external `kill -INT` still can, and the
// default action for either signal skips atexit handlers entirely, which
// would leave a real user's shell in raw mode until they ran `stty sane`.
static volatile sig_atomic_t abc802_quit_requested = 0;
static volatile sig_atomic_t abc802_quit_signal = 0;

static void abc802_handle_quit_signal(int sig) {
    abc802_quit_requested = 1;
    abc802_quit_signal = sig;
}

// Non-blocking single-byte stdin read. Returns -1 if nothing is waiting.
static int poll_stdin_byte(void) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = {0, 0};
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0) return -1;
    uint8_t byte;
    ssize_t n = read(STDIN_FILENO, &byte, 1);
    return (n == 1) ? byte : -1;
}

// A host terminal sends Å/Ä/Ö/Ü/É (and lowercase) as 2-byte UTF-8
// sequences (0xC2-0xDF lead, 0x80-0xBF continuation). The ABC802's whole
// character set lives in the Latin-1 Supplement block, which UTF-8 always
// encodes in exactly 2 bytes, so no 3-/4-byte lead bytes need recognizing.
// Buffer the lead byte, wait for its continuation, then look the
// codepoint up via render.c's own charset table.
//
// ESC-introduced sequences (a modern terminal's arrow keys, function
// keys, and so on) are recognized only well enough to be *discarded*, not
// translated. That is deliberate. The ABC80 target does translate its
// arrow keys, because disassembling that ROM's line editor established
// exactly which two bytes it wants (0x08/0x09). The ABC802's editor is a
// different one, and probing it with the obvious candidates found no
// non-destructive cursor-right at all: 0x09 and 0x1F are ignored, and
// 0x0C clears the screen. Rather than invent a mapping, this drops them -
// harmless, since the ROM ignores unrecognized control bytes anyway - and
// ABC802_ROADMAP.md records it as a known gap for whoever disassembles
// that editor.
#define ABC802_ESC_SEQUENCE_TIMEOUT_SEC 0.05

// Every common terminal's Backspace key sends DEL (0x7F), not BS (0x08).
// The ABC802 ROM's line editor implements a real destructive delete on
// 0x08 (verified: "PRINT 12" + two 0x08 + "3" evaluates 3) but treats
// 0x7F as an ordinary printable character, echoing a blank into the line
// (verified the same way). Untranslated, a user's Backspace key would
// silently corrupt what they typed instead of erasing it, so DEL is
// rewritten to BS here. This overrides nothing useful: 0x7F has no
// editing meaning to this ROM at all.
#define ABC802_DEL 0x7F
#define ABC802_BS  0x08

static double elapsed_since(const struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start->tv_sec) +
           (double)(now.tv_nsec - start->tv_nsec) / 1e9;
}

static int poll_keyboard_byte(void) {
    static enum { ESC_NONE, ESC_SEEN, ESC_BRACKET } esc_state = ESC_NONE;
    static struct timespec esc_started;
    static int utf8_lead = -1;
    static struct timespec utf8_started;

    if (esc_state != ESC_NONE && elapsed_since(&esc_started) > ABC802_ESC_SEQUENCE_TIMEOUT_SEC) {
        esc_state = ESC_NONE; // gave up - a lone ESC, or an unrecognized sequence
    }
    if (utf8_lead >= 0 && elapsed_since(&utf8_started) > ABC802_ESC_SEQUENCE_TIMEOUT_SEC) {
        utf8_lead = -1;       // gave up - a lone/malformed lead byte
    }

    int b = poll_stdin_byte();

    if (esc_state == ESC_NONE) {
        if (utf8_lead >= 0) {
            if (b < 0) return -1; // mid-sequence, still waiting
            int lead = utf8_lead;
            utf8_lead = -1;
            if ((b & 0xC0) != 0x80) return -1; // not a valid continuation byte
            uint32_t codepoint = ((uint32_t)(lead & 0x1F) << 6) | (uint32_t)(b & 0x3F);
            return abc802_charset_byte_for_codepoint(codepoint);
        }
        if (b == 0x1B) {
            esc_state = ESC_SEEN;
            clock_gettime(CLOCK_MONOTONIC, &esc_started);
            return -1;
        }
        if (b >= 0xC2 && b <= 0xDF) {
            utf8_lead = b;
            clock_gettime(CLOCK_MONOTONIC, &utf8_started);
            return -1;
        }
        if (b == ABC802_DEL) return ABC802_BS;
        return b;
    }

    if (b < 0) return -1; // mid-sequence, still waiting for the next byte

    if (esc_state == ESC_SEEN) {
        esc_state = (b == '[') ? ESC_BRACKET : ESC_NONE;
        return (esc_state == ESC_BRACKET) ? -1 : b;
    }

    esc_state = ESC_NONE; // ESC_BRACKET: a CSI sequence, deliberately dropped
    return -1;
}

static void usage(const char *argv0) {
    printf("Usage: %s [options]\n", argv0);
    printf("\nOptions:\n");
    printf("  --rom-dir DIR    ROM directory (default: %s)\n", DEFAULT_ROM_DIR);
    printf("  --dos-rom FILE   DOS/option ROM image (default: %s)\n", DEFAULT_DOS_ROM);
    printf("  --cycles N       stop after N T-states (default: 20000000)\n");
    printf("  --screen         print the text screen when the run ends\n");
    printf("  --profile        print the most-executed addresses when the run ends\n");
    printf("  --type TEXT      send TEXT to the keyboard once the ROM is ready for it\n");
    printf("  --interactive    live keyboard input (raw terminal) and a real-time,\n");
    printf("                   continuously redrawn screen; Ctrl-C reaches BASIC,\n");
    printf("                   Ctrl-\\ exits. Runs at real ABC802 speed, uncapped\n");
    printf("                   unless --cycles is given explicitly.\n");
    printf("  --columns 40|80  characters per line (DIP S3, default 40 as on MAME)\n");
    printf("  -h, --help       this message\n");
    printf("\nEnvironment:\n");
    printf("  ABC802_TRACE_IO=1   log every I/O port access to stderr\n");
}

int main(int argc, char **argv) {
    const char *rom_dir = DEFAULT_ROM_DIR;
    const char *dos_rom = DEFAULT_DOS_ROM;
    long long max_cycles = 20000000;
    int show_screen = 0;
    int show_profile = 0;
    const char *type_text = NULL;
    int columns = 40;
    bool interactive = false;
    bool cycles_given = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "--rom-dir") && i + 1 < argc) {
            rom_dir = argv[++i];
        } else if (!strcmp(argv[i], "--dos-rom") && i + 1 < argc) {
            dos_rom = argv[++i];
        } else if (!strcmp(argv[i], "--cycles") && i + 1 < argc) {
            max_cycles = atoll(argv[++i]);
            cycles_given = true;
        } else if (!strcmp(argv[i], "--screen")) {
            show_screen = 1;
        } else if (!strcmp(argv[i], "--profile")) {
            show_profile = 1;
        } else if (!strcmp(argv[i], "--type") && i + 1 < argc) {
            type_text = argv[++i];
        } else if (!strcmp(argv[i], "--interactive")) {
            interactive = true;
        } else if (!strcmp(argv[i], "--columns") && i + 1 < argc) {
            columns = atoi(argv[++i]);
            if (columns != 40 && columns != 80) {
                fprintf(stderr, "--columns must be 40 or 80\n");
                return 1;
            }
        } else {
            fprintf(stderr, "Unknown option '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    // A live session ends when the user asks it to (Ctrl-\), not when a
    // T-state budget runs out - at real ABC802 speed the default 20M cap
    // is under seven seconds of wall clock. An explicit --cycles still
    // wins, so a bounded interactive run stays possible.
    if (interactive && !cycles_given) max_cycles = LLONG_MAX;

    static uint8_t ram[RAM_SIZE];
    memset(ram, 0, sizeof(ram));

    // cpu.memory and the `ram` argument passed to z80_execute() must be the
    // *same* buffer - opcode fetch reads `ram` directly while operand access
    // goes through z80_read_byte()/cpu.memory. Same requirement the ABC80 and
    // CP/M targets document in their own main.c.
    Z80 cpu = {0};
    cpu.memory = ram;
    cpu.pc = 0x0000;
    z80_init_tables();

    if (!abc802_memory_init(&cpu, rom_dir, dos_rom)) return 1;
    abc802_memory_attach(&cpu);
    abc802_ports_attach(&cpu);
    abc802_set_config(columns == 80, true);

    printf("ABC802: loaded 32K ROM from '%s' (DOS ROM '%s')\n", rom_dir, dos_rom);

    if (interactive) {
        abc802_console_init();
        struct sigaction sa = {0};
        sa.sa_handler = abc802_handle_quit_signal;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGQUIT, &sa, NULL);
        printf("Interactive - Ctrl-C reaches BASIC, Ctrl-\\ exits.\n");
    }

    // Per-address execution counts. A flat 64K array of counters is the
    // simplest thing that answers "where is it actually spending its time",
    // which is the first question to ask of a ROM that runs but produces
    // nothing - the same reason abc80/emu/src/main.c keeps its own visited[]
    // array.
    static long long pc_hits[RAM_SIZE];

    // Keyboard feed. Bytes are handed over one at a time, each only once
    // the ROM has actually consumed the previous one - the DART holds a
    // single receive byte, so typing faster than the ROM reads would just
    // overwrite it. The gap is measured in T-states rather than
    // instructions so it tracks emulated time.
    size_t type_pos = 0;
    size_t type_len = type_text ? strlen(type_text) : 0;
    long long next_key_at = 0;
    // Gap between keystrokes, in T-states (~0.1s of emulated time at
    // 3 MHz). Generous on purpose: the ROM discards input while it is
    // still initializing after the sign-on banner, so typing at a
    // realistic human pace is what makes the first characters survive.
    //
    // --interactive enforces the same gap, and needs it just as much. A
    // human at a keyboard could never beat it, but a pipe or a paste
    // delivers a whole line at once, and the DART holds exactly one
    // receive byte - without the gap those bytes overwrite each other and
    // most of the line is simply lost. Unsent input waits in the host's
    // own terminal/pipe buffer until the gap expires, so nothing is
    // dropped; it is drained at the speed the real machine could accept
    // it. (Found exactly that way: piping "PRINT 6*7" into an early
    // version of --interactive reached BASIC as nothing at all.)
    const long long key_gap = 300000;

    // --interactive-only pacing/rendering state. run_start_time is the
    // wall-clock origin cycles/ABC802_CLOCK_HZ is paced against;
    // last_render_sec is the elapsed-real-seconds value the last frame was
    // drawn at, so ABC802_RENDER_INTERVAL_SEC throttles redraws
    // independently of how often the pacing check itself runs.
    struct timespec run_start_time = {0, 0};
    double last_render_sec = -1.0;
    if (interactive) clock_gettime(CLOCK_MONOTONIC, &run_start_time);

    long long cycles = 0;
    long long instructions = 0;
    bool halted = false;
    while (!abc802_quit_requested && cycles < max_cycles) {
        // Stands in for the real M1 line - see memory.c's header comment.
        // Must happen before the instruction runs, since it is that
        // instruction's own data reads that consult it.
        abc802_note_instruction_fetch(cpu.pc);
        if (show_profile) pc_hits[cpu.pc]++;
        int taken = z80_execute(&cpu, ram);
        if (taken < 0) {
            fprintf(stderr, "Halted: unimplemented opcode at PC=%04X\n", cpu.pc);
            halted = true;
            break;
        }
        cycles += taken;
        instructions++;
        abc802_ports_tick(&cpu, taken);

        if (type_pos < type_len && cycles >= next_key_at &&
            abc802_keyboard_ready() && !abc802_keyboard_busy()) {
            char ch = type_text[type_pos++];
            abc802_keyboard_send((uint8_t)(ch == '\n' ? 0x0D : ch));
            // ~10ms of emulated time between keystrokes at 3 MHz, which is
            // slower than any real typist and leaves the ROM ample room to
            // process each one.
            next_key_at = cycles + key_gap;
        }

        // Live keyboard, on exactly the same terms as the --type feed
        // above: the same inter-key gap, and the same "the DART holds one
        // receive byte" busy check. stdin is deliberately *not* read
        // ahead of that gate - an unread byte simply waits in the host's
        // terminal or pipe buffer, which is what keeps a fast paste
        // intact instead of overwriting itself (see key_gap). A --type
        // string, if one was also given, is fed first by the block above:
        // the two coexist, so a session can be seeded with a command and
        // then taken over by hand.
        if (interactive && type_pos >= type_len && cycles >= next_key_at &&
            abc802_keyboard_ready() && !abc802_keyboard_busy()) {
            int key = poll_keyboard_byte();
            if (key >= 0) {
                abc802_keyboard_send((uint8_t)key);
                next_key_at = cycles + key_gap;
            }
            // A -1 return deliberately does *not* start a new gap. It
            // means poll_keyboard_byte() consumed part of a multi-byte
            // sequence (a UTF-8 lead byte, or an ESC) and needs the rest
            // promptly: those sequences time out after
            // ABC802_ESC_SEQUENCE_TIMEOUT_SEC of *real* time, which is
            // shorter than key_gap is at real ABC802 speed, so gating the
            // continuation byte behind the gap would expire every one of
            // them and no accented letter would ever arrive.
        }

        // Real-time pacing and live rendering (see
        // ABC802_PACING_CHECK_INTERVAL for why this is periodic rather
        // than per-instruction). If the emulated machine has raced ahead
        // of real elapsed time, sleep off the difference; either way,
        // redraw at most every ABC802_RENDER_INTERVAL_SEC. No blink phase
        // is computed here - the ROM blinks its own cursor through the
        // CRTC, so pacing execution correctly is what makes the blink
        // look right (see render.h).
        if (interactive && (instructions % ABC802_PACING_CHECK_INTERVAL) == 0) {
            double elapsed_real = elapsed_since(&run_start_time);
            double elapsed_emulated = (double)cycles / ABC802_CLOCK_HZ;

            if (elapsed_emulated > elapsed_real) {
                double sleep_sec = elapsed_emulated - elapsed_real;
                struct timespec req;
                req.tv_sec = (time_t)sleep_sec;
                req.tv_nsec = (long)((sleep_sec - (double)req.tv_sec) * 1e9);
                nanosleep(&req, NULL);
                elapsed_real = elapsed_emulated;
            }

            if (elapsed_real - last_render_sec >= ABC802_RENDER_INTERVAL_SEC) {
                abc802_render_frame(stdout);
                last_render_sec = elapsed_real;
            }
        }
    }

    // Final frame before the summary, not after: abc802_render_frame()
    // clears the screen first thing, which would otherwise wipe the
    // summary a user is trying to read after pressing Ctrl-\.
    if (interactive) abc802_render_frame(stdout);

    printf("Ran %lld instructions / %lld T-states; PC=%04X (%s)\n",
           instructions, cycles, cpu.pc,
           halted ? "halted on unimplemented opcode"
           : abc802_quit_requested
               ? (abc802_quit_signal == SIGQUIT ? "user requested exit (Ctrl-\\)"
                                                : "user requested exit (SIGINT)")
               : "reached T-state cap");
    printf("CRTC programmed: %s (R1=%d cols, R6=%d rows)\n",
           abc802_crtc_programmed() ? "yes" : "no",
           abc802_crtc_reg(1), abc802_crtc_reg(6));

    if (show_profile) {
        printf("\nMost-executed addresses:\n");
        for (int rank = 0; rank < 20; rank++) {
            int best = -1;
            for (int a = 0; a < RAM_SIZE; a++) {
                if (pc_hits[a] > 0 && (best < 0 || pc_hits[a] > pc_hits[best])) best = a;
            }
            if (best < 0) break;
            printf("  %04X  %lld\n", best, pc_hits[best]);
            pc_hits[best] = 0;
        }
    }

    {
        const uint8_t *cr = abc802_char_ram();
        int nonzero = 0, nonspace = 0;
        for (int i = 0; i < ABC802_CHAR_RAM_SIZE; i++) {
            if (cr[i] != 0) nonzero++;
            if (cr[i] != 0 && cr[i] != 0x20) nonspace++;
        }
        printf("Character RAM: %d/%d nonzero, %d non-space\n",
               nonzero, ABC802_CHAR_RAM_SIZE, nonspace);
    }

    if (show_screen) abc802_render_text_screen(stdout);
    return halted ? EXIT_FAILURE : EXIT_SUCCESS;
}
