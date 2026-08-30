// abc806/emu/src/main.c - bin/abc806, the Luxor ABC806 machine target.
//
// A fourth Z80 machine alongside abc80/, abc802/ and cpm/, sharing the
// same proven core (z80core/z80.o + alu.o). See
// abc806/docs/ABC806_ROADMAP.md for status and ABC806_SCOPING.md for the
// plan being followed.
//
// The terminal glue below (raw mode, the ESC/UTF-8 input state machine,
// real-time pacing) is deliberately a near-copy of the ABC802's, on the
// same terms emu/src/ports.c is: each machine target owning its own
// console glue is this repository's standing choice, and extracting a
// shared one waits until it is known what is genuinely common.

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
#include "../../../abcbus/disk.h"
#include "chargen.h"
#include "memory.h"
#include "png.h"
#include "ports.h"
#include "render.h"
#include "rtc.h"
#include "step.h"

#define DEFAULT_ROM_DIR "abc806/resources/rom"
#define DEFAULT_DOS_ROM "ABC806-dos.66-31.bin"

// The real ABC806 Z80 runs at 3 MHz, and --interactive converts T-states
// into real seconds against it. Same clock as the ABC802.
#define ABC806_CLOCK_HZ 3000000.0

// Pacing is checked every N instructions rather than every one:
// clock_gettime() and nanosleep() are syscalls, and three million a second
// would swamp the emulation itself.
#define ABC806_PACING_CHECK_INTERVAL 500
#define ABC806_RENDER_INTERVAL_SEC (1.0 / 30.0)

// The attribute plane's flash bit needs a phase, and unlike the ABC802's
// cursor (which that ROM blinks in software through the CRTC) nothing in
// the machine supplies one - flash is hardware here, driven off the frame
// rate. 2 Hz is the conventional rate and is stated as the assumption it
// is: no source consulted gives the ABC806's own divider.
#define ABC806_FLASH_HZ 2.0

// A multi-byte terminal sequence must complete inside this much *real*
// time or it is abandoned. Shorter than the inter-key gap on purpose; see
// where it is used.
#define ABC806_ESC_SEQUENCE_TIMEOUT_SEC 0.05

// Every common terminal's Backspace sends DEL (0x7F). Whether this ROM's
// line editor treats that as an edit or as a printable character has not
// been swept the way the ABC802's was, so DEL is rewritten to BS (0x08),
// which every ABC800-family editor does implement destructively. If the
// sweep is ever done and finds 0x7F meaningful, this is the line to
// revisit.
#define ABC806_DEL 0x7F
#define ABC806_BS  0x08

// Terminal mode. Character-at-a-time input, no host echo (the ROM echoes
// through character RAM itself), a real Enter arriving as 0x0D rather than
// being rewritten to 0x0A, and Ctrl-S not swallowed as XOFF. VINTR is
// disabled so a real Ctrl-C reaches the ROM's own BASIC, where a break
// belongs; ISIG stays on, so Ctrl-\ remains this tool's quit key.
static struct termios orig_termios;
static int termios_saved = 0;

static void console_shutdown(void) {
    if (termios_saved) tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

static void console_init(void) {
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &orig_termios) != 0) return;
    termios_saved = 1;
    atexit(console_shutdown);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_iflag &= ~(ICRNL | INLCR | IGNCR);
    raw.c_iflag &= ~IXON;
    raw.c_cc[VINTR] = _POSIX_VDISABLE;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

// Set by the handler and checked by the main loop, so a quit leaves
// through the normal end-of-main path - which is what runs the summary
// and, critically, the atexit terminal restore. SIGINT is caught as well
// as SIGQUIT: disabling VINTR stops the *driver* raising it, but an
// external `kill -INT` still can, and the default action for either skips
// atexit handlers and would strand a real user's shell in raw mode.
static volatile sig_atomic_t quit_requested = 0;
static volatile sig_atomic_t quit_signal = 0;

static void handle_quit_signal(int sig) {
    quit_requested = 1;
    quit_signal = sig;
}

static double elapsed_since(const struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start->tv_sec) +
           (double)(now.tv_nsec - start->tv_nsec) / 1e9;
}

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

// One machine character from the host terminal, or -1 if none is ready
// yet. Two multi-byte shapes have to be reassembled: a 2-byte UTF-8
// sequence for the Swedish letters (this machine's whole character set is
// in the Latin-1 Supplement block, so 3- and 4-byte leads need no
// handling), and ESC-introduced sequences from arrow and function keys.
//
// Of the arrow keys only Left is translated, to BS. Right is dropped: the
// ABC802's editor was swept byte by byte and turned out to have no cursor
// movement at all, and this ROM is from the same family and the same year.
// That is an inference rather than a sweep, and it is flagged as one - the
// honest version of this comment on the ABC802 rests on evidence this one
// does not have yet.
static int poll_keyboard_byte(void) {
    static enum { ESC_NONE, ESC_SEEN, ESC_BRACKET } esc_state = ESC_NONE;
    static struct timespec esc_started;
    static int utf8_lead = -1;
    static struct timespec utf8_started;

    if (esc_state != ESC_NONE && elapsed_since(&esc_started) > ABC806_ESC_SEQUENCE_TIMEOUT_SEC)
        esc_state = ESC_NONE;   // a lone ESC, or a sequence we do not know
    if (utf8_lead >= 0 && elapsed_since(&utf8_started) > ABC806_ESC_SEQUENCE_TIMEOUT_SEC)
        utf8_lead = -1;         // a lone or malformed lead byte

    int b = poll_stdin_byte();

    if (esc_state == ESC_NONE) {
        if (utf8_lead >= 0) {
            if (b < 0) return -1;              // mid-sequence, still waiting
            int lead = utf8_lead;
            utf8_lead = -1;
            if ((b & 0xC0) != 0x80) return -1; // not a continuation byte
            uint32_t cp = ((uint32_t)(lead & 0x1F) << 6) | (uint32_t)(b & 0x3F);
            return abc806_charset_byte_for_codepoint(cp);
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
        if (b == ABC806_DEL) return ABC806_BS;
        return b;
    }

    if (b < 0) return -1;   // mid-sequence

    if (esc_state == ESC_SEEN) {
        esc_state = (b == '[') ? ESC_BRACKET : ESC_NONE;
        return (esc_state == ESC_BRACKET) ? -1 : b;
    }

    esc_state = ESC_NONE;
    if (b == 'D') return ABC806_BS;
    return -1;
}

static void usage(const char *argv0) {
    printf("Usage: %s [options]\n\n", argv0);
    printf("Options:\n");
    printf("  --rom-dir DIR    ROM directory (default: %s)\n", DEFAULT_ROM_DIR);
    printf("  --dos-rom FILE   DOS PROM at 0x6000 (default: %s)\n", DEFAULT_DOS_ROM);
    printf("  --cycles N       stop after N T-states (default: 20000000)\n");
    printf("  --disk FILE      attach FILE as a floppy image on the ABC-bus\n");
    printf("  --type TEXT      send TEXT to the keyboard once the ROM is ready\n");
    printf("  --type-at N      hold TEXT back until N T-states have run. The ROM\n");
    printf("                   reports the keyboard ready long before it is\n");
    printf("                   listening, and discards anything typed meanwhile\n");
    printf("  --interactive    live session: real 3 MHz pacing, a screen redrawn\n");
    printf("                   in colour at 30fps, and a real keyboard. Removes\n");
    printf("                   the T-state cap unless --cycles was given too.\n");
    printf("                   Quit with Ctrl-\\ (Ctrl-C reaches BASIC instead)\n");
    printf("  --screen         print the text screen when the run ends\n");
    printf("  --screenshot F   write the screen as a real PNG to F - actual\n");
    printf("                   pixels from the character ROM and the RAD PROM,\n");
    printf("                   including the attributes --screen cannot show\n");
    printf("  --profile        print the most-executed addresses when the run ends\n");
    printf("  -h, --help       this message\n");
    printf("\nEnvironment:\n");
    printf("  ABC806_TRACE_IO=1      log every I/O port access to stderr\n");
    printf("  ABC806_TRACE_WRITES=1  log every CPU write, with the EME/KEYDTR/HRS\n");
    printf("                         state that decides where it lands\n");
    printf("  ABC806_PROFILE_ALL=1   with --profile, dump every executed address\n");
    printf("                         and its count, for a differential profile\n");
}

int main(int argc, char **argv) {
    const char *rom_dir = DEFAULT_ROM_DIR;
    const char *dos_rom = DEFAULT_DOS_ROM;
    long long max_cycles = 20000000;
    bool cycles_given = false;
    bool interactive = false;
    bool profile = false;
    bool show_screen = false;
    const char *screenshot_path = NULL;
    const char *type_text = NULL;
    long long type_at = 0;
    const char *disk_paths[8];
    int disk_count = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "--rom-dir") && i + 1 < argc) {
            rom_dir = argv[++i];
        } else if (!strcmp(argv[i], "--dos-rom") && i + 1 < argc) {
            dos_rom = argv[++i];
        } else if (!strcmp(argv[i], "--cycles") && i + 1 < argc) {
            cycles_given = true;
            max_cycles = atoll(argv[++i]);
        } else if (!strcmp(argv[i], "--disk") && i + 1 < argc) {
            if (disk_count < 8) disk_paths[disk_count++] = argv[++i];
            else { fprintf(stderr, "Too many --disk arguments\n"); return 1; }
        } else if (!strcmp(argv[i], "--type") && i + 1 < argc) {
            type_text = argv[++i];
        } else if (!strcmp(argv[i], "--type-at") && i + 1 < argc) {
            type_at = atoll(argv[++i]);
        } else if (!strcmp(argv[i], "--interactive")) {
            interactive = true;
            if (!cycles_given) max_cycles = 0;   // 0 = run until quit
        } else if (!strcmp(argv[i], "--screen")) {
            show_screen = true;
        } else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            screenshot_path = argv[++i];
        } else if (!strcmp(argv[i], "--profile")) {
            profile = true;
        } else {
            fprintf(stderr, "Unknown argument '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    static uint8_t ram[RAM_SIZE];
    Z80 cpu = {0};
    cpu.memory = ram;
    z80_init_tables();

    if (!abc806_memory_init(&cpu, rom_dir, dos_rom)) return 1;
    abc806_memory_attach(&cpu);
    abc806_ports_attach(&cpu);
    abc806_rtc_init();

    for (int d = 0; d < disk_count; d++)
        if (!abcbus_disk_attach_arg(disk_paths[d])) return 1;

    printf("ABC806: loaded 32K ROM from '%s' (DOS PROM '%s')\n", rom_dir, dos_rom);
    if (disk_count > 0) {
        printf("ABC-bus: %s floppy controller, %d drive%s attached, interleave %u\n",
               abcbus_disk_type_name(), abcbus_disk_attached_count(),
               abcbus_disk_attached_count() == 1 ? "" : "s",
               abcbus_disk_interleave());
    }

    static unsigned long long hits[0x10000];
    long long cycles = 0;
    long long instructions = 0;
    bool halted = false;

    // Keystrokes are paced the same way the ABC802's are, and for the same
    // hardware reason: the DART holds exactly one received byte, so a
    // burst would overwrite itself. ~0.1s of emulated time per key.
    //
    // --interactive enforces the identical gap and needs it just as much.
    // No human can beat it, but a pipe or a paste delivers a whole line at
    // once and most of it would be lost. Unsent input waits in the host's
    // own terminal buffer until the gap expires, so nothing is dropped -
    // it is drained at the speed the real machine could accept it.
    const long long key_gap = 300000;

    // --type's text, converted from host UTF-8 into the machine's own
    // character bytes rather than fed through raw. Feeding it raw is the
    // bug docs/postmortems/2026-08-28-type-raw-utf8-bytes.md is about, and this
    // target had it: `--type 'PRINT "ÅÄÖ"'` reached BASIC as the UTF-8
    // bytes and errored, while an interactive session typing the same
    // letters worked. Starting a new target's main.c from a blank page
    // rather than from the one that already fixed this is how a solved
    // problem comes back.
    static uint8_t type_chars[4096];
    size_t type_pos = 0;
    size_t type_len = type_text
        ? abc806_utf8_to_chars(type_text, type_chars, sizeof type_chars) : 0;
    long long next_key_at = type_at;

    // The ROM reports the keyboard ready long before it is listening, and
    // discards whatever arrives meanwhile - typing at T-state 0 reaches
    // BASIC as `INT 6*7`, the first two characters simply gone. So --type
    // additionally waits for the machine to have *drawn* something: the
    // sign-on is written at the very end of boot, immediately before the
    // keyboard poll loop, which makes "there is a non-space character on
    // screen" a real readiness signal rather than a tuned delay constant.
    //
    // --type-at is still there and still needed, for a different problem:
    // a program booting off disk is listening long after the ROM's own
    // sign-on, and this gate cannot see that.
    bool type_gate_open = false;

    // Pacing and rendering state. run_start is the wall-clock origin that
    // cycles/ABC806_CLOCK_HZ is paced against; last_render_sec throttles
    // redraws independently of how often the pacing check runs.
    struct timespec run_start = {0, 0};
    double last_render_sec = -1.0;
    if (interactive) {
        console_init();
        signal(SIGINT, handle_quit_signal);
        signal(SIGQUIT, handle_quit_signal);
        clock_gettime(CLOCK_MONOTONIC, &run_start);
    }

    while (!quit_requested && (max_cycles == 0 || cycles < max_cycles)) {
        if (!type_gate_open && type_pos < type_len && abc806_crtc_programmed()) {
            const uint8_t *cram = abc806_char_ram();
            for (int i = 0; i < ABC806_CHAR_RAM_SIZE; i++) {
                uint8_t ch = cram[i] & 0x7F;
                if (ch > 0x20 && ch < 0x7F) { type_gate_open = true; break; }
            }
        }
        if (type_gate_open && type_pos < type_len && cycles >= next_key_at &&
            !abc806_keyboard_busy() && abc806_keyboard_ready()) {
            abc806_keyboard_send(type_chars[type_pos++]);
            next_key_at = cycles + key_gap;
        }

        // The live keyboard, on exactly the same terms as the --type feed:
        // same gap, same "the DART holds one byte" busy check. stdin is
        // deliberately not read ahead of that gate - an unread byte waits
        // in the host's buffer, which is what keeps a fast paste intact.
        // A --type string, if given, is fed first, so a session can be
        // seeded with a command and then taken over by hand.
        if (interactive && type_pos >= type_len && cycles >= next_key_at &&
            !abc806_keyboard_busy() && abc806_keyboard_ready()) {
            int key = poll_keyboard_byte();
            if (key >= 0) {
                abc806_keyboard_send((uint8_t)key);
                next_key_at = cycles + key_gap;
            }
            // A -1 deliberately does *not* start a new gap: it means part
            // of a multi-byte sequence was consumed and the rest is needed
            // promptly. Those sequences time out after
            // ABC806_ESC_SEQUENCE_TIMEOUT_SEC of real time, which is
            // shorter than key_gap is at 3 MHz, so gating the continuation
            // byte behind the gap would expire every one of them and no
            // accented letter would ever arrive.
        }

        if (profile) hits[cpu.pc]++;
        int taken = abc806_step(&cpu, ram, &cycles);
        if (taken < 0) {
            printf("Unimplemented opcode at PC=%04X; stopping\n", cpu.pc);
            halted = true;
            break;
        }
        instructions++;

        if (interactive && (instructions % ABC806_PACING_CHECK_INTERVAL) == 0) {
            double elapsed_real = elapsed_since(&run_start);
            double elapsed_emulated = (double)cycles / ABC806_CLOCK_HZ;

            // If the emulated machine has raced ahead of real time, sleep
            // off the difference. This is what makes the machine feel like
            // a 3 MHz machine rather than finishing before a key can be
            // pressed.
            if (elapsed_emulated > elapsed_real) {
                double sleep_sec = elapsed_emulated - elapsed_real;
                struct timespec req;
                req.tv_sec = (time_t)sleep_sec;
                req.tv_nsec = (long)((sleep_sec - (double)req.tv_sec) * 1e9);
                nanosleep(&req, NULL);
                elapsed_real = elapsed_emulated;
            }

            if (elapsed_real - last_render_sec >= ABC806_RENDER_INTERVAL_SEC) {
                bool flash_on = ((long)(elapsed_real * ABC806_FLASH_HZ * 2) & 1) == 0;
                abc806_render_frame(stdout, flash_on);
                last_render_sec = elapsed_real;
            }
        }
    }

    // A final frame before the summary, not after: render_frame() clears
    // the screen first thing, which would otherwise wipe the summary a
    // user is trying to read after pressing Ctrl-\.
    if (interactive) {
        abc806_render_frame(stdout, true);
        if (quit_signal)
            printf("Stopped by signal %d.\n", (int)quit_signal);
    }

    printf("Ran %lld instructions / %lld T-states; PC=%04X (%s)\n",
           instructions, cycles, cpu.pc,
           halted ? "halted" : "reached T-state cap");

    // The milestone gate.
    if (abc806_crtc_programmed()) {
        printf("CRTC programmed: yes (R1=%d cols, R6=%d rows)\n",
               abc806_crtc_reg(1), abc806_crtc_reg(6));
    } else {
        printf("CRTC programmed: no - the ROM did not get that far\n");
    }

    // A second, independent sign that the boot did real work rather than
    // merely surviving: the ABC802's own milestone 1 printed the same
    // summary. A CRTC programmed but a blank character RAM would mean the
    // ROM configured the video and then wedged before drawing anything.
    {
        const uint8_t *cram = abc806_char_ram();
        const uint8_t *aram = abc806_attr_ram();
        int nonzero = 0, nonspace = 0, attrs = 0;
        for (int i = 0; i < ABC806_CHAR_RAM_SIZE; i++) {
            if (cram[i]) nonzero++;
            if ((cram[i] & 0x7F) != 0x20 && cram[i]) nonspace++;
            if (aram[i]) attrs++;
        }
        printf("Character RAM: %d/%d nonzero, %d non-space; "
               "attribute plane: %d nonzero\n",
               nonzero, ABC806_CHAR_RAM_SIZE, nonspace, attrs);

        // The high-resolution plane, reported the same way and for the
        // same reason: it is the one part of this machine whose state is
        // invisible in a text dump, so without this line a graphics
        // command that silently did nothing looks exactly like one that
        // worked.
        int hr_nonzero = 0;
        for (int i = 0; i < ABC806_VIDEO_RAM_SIZE; i++)
            if (abc806_videoram_read((uint32_t)i)) hr_nonzero++;
        printf("High-resolution plane: %d/%d bytes nonzero\n",
               hr_nonzero, ABC806_VIDEO_RAM_SIZE);
    }

    // One snapshot, assembled in one place (render.c), so --screen,
    // --screenshot and the live frame cannot disagree about what the
    // screen currently is.
    Abc806Screen screen;
    abc806_current_screen(&screen, false);

    if (show_screen) abc806_render_text_screen(stdout);

    if (screenshot_path) {
        static uint8_t pixels[ABC806_MAX_PIXELS];
        int w = abc806_pixel_width(&screen);
        int h = abc806_pixel_height(&screen);
        static uint32_t pal[8];
        for (int i = 0; i < 8; i++) pal[i] = abc806_palette(i);
        if (!abc806_render_pixels(&screen, pixels, sizeof pixels)) {
            fprintf(stderr, "Screenshot failed: CRTC not programmed, or screen too large\n");
        } else if (!abc806_write_png(screenshot_path, pixels, w, h, pal, 8)) {
            fprintf(stderr, "Screenshot failed: could not write '%s'\n", screenshot_path);
        } else {
            printf("Screenshot: %s (%dx%d)\n", screenshot_path, w, h);
        }
    }

    if (profile) {
        // ABC806_PROFILE_ALL dumps every executed address and its count,
        // which is what a differential profile (run with a command, run
        // without, diff the sets) needs to locate a ROM routine.
        if (getenv("ABC806_PROFILE_ALL")) {
            for (int a = 0; a < 0x10000; a++)
                if (hits[a]) printf("A %04X %llu\n", a, hits[a]);
            return halted ? EXIT_FAILURE : EXIT_SUCCESS;
        }
        printf("Most-executed addresses:\n");
        for (int n = 0; n < 20; n++) {
            int best = -1;
            for (int a = 0; a < 0x10000; a++)
                if (hits[a] && (best < 0 || hits[a] > hits[best])) best = a;
            if (best < 0) break;
            printf("  %04X  %llu\n", best, hits[best]);
            hits[best] = 0;
        }
    }

    return halted ? EXIT_FAILURE : EXIT_SUCCESS;
}
