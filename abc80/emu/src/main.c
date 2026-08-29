// abc80/emu/src/main.c - entry point for the Luxor ABC80 machine target:
// loads the real BASIC ROM images and runs them on the shared Z80 core
// (z80core/z80.c/alu.c) via z80_execute() directly - never z80_step(),
// since that's CP/M's own wrapper and intercepts addresses (0x0000,
// 0x0005) that legitimately hold real ABC80 ROM code. See
// abc80/docs/ABC80_ROADMAP.md for the full memory map, sources, and what's
// still missing (ABCbus expansion).
//
// Keyboard input (Milestone 3, keyboard.c) reads stdin non-blockingly and
// feeds PIO Port A, the register the real ROM's own steady-state polling
// loop reads (confirmed via this project's own disassembler - see
// keyboard.c's top comment for the exact instructions). Cassette storage
// (Milestone 4, cassette.c) is a "quickload"/"quicksave" bypass of BASIC's
// own program-storage pointers rather than real analog tape emulation -
// see cassette.c's own top comment for why. Sound (Milestone 5, sound.c)
// logs every real write to the SN76477 control port (0x06) and, if
// requested, renders the resulting tone activity to a WAV file - there's
// no live audio output in this environment, so a WAV file is the
// practical, verifiable deliverable (see sound.c's own top comment).
// Renders whatever the ROM wrote to video RAM (0x7C00-0x7FFF, directly
// addressable within the flat `ram` array - no separate buffer needed)
// via render.c's terminal backend once the run ends, either from an
// unimplemented-opcode halt (a real bug) or the safety instruction cap
// (expected if nothing was typed).

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <termios.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <limits.h>

#include "../../../z80core/z80.h"
#include "render.h"
#include "video_timing.h"
#include "keyboard.h"
#include "charset.h"
#include "cassette.h"
#include "sound.h"
#include "abcbus.h"
#include "../../../abcbus/disk.h"
#include "step.h"

// Real ABC80 Z80 clock (11.9808 MHz crystal / 2 / 2 - see MAME's
// abc80_state::abc80_common(): `Z80(config, m_maincpu,
// XTAL(11'980'800)/2/2)`), needed to convert T-states into real seconds
// for sound.c's WAV rendering and, below, for --interactive's real-time
// pacing and cursor blink.
#define ABC80_CLOCK_HZ 2995200.0

// --interactive real-time pacing/rendering. Batch (default) mode runs
// z80_execute() as fast as the host can go - fine for a one-shot debug
// run, but wrong for a live session: at full host speed, the default
// 5,000,000-instruction cap alone would complete in well under a second
// of wall-clock time, ending the "interactive" session before a human
// could ever react. --interactive instead throttles execution to real
// ABC80 speed and removes the fixed cap (see main()'s own argument
// handling), checked/corrected every ABC80_PACING_CHECK_INTERVAL
// instructions rather than after every single one - clock_gettime() and
// nanosleep() are real syscalls, and 2,995,200 of them a second would
// swamp actual emulation work. At ABC80_CLOCK_HZ's own ~13 T-states/
// instruction average, this interval is roughly 2ms of emulated time
// between pacing corrections/render checks - fine-grained enough for
// smooth output and prompt keyboard response (poll_stdin_byte() itself
// still runs every single loop iteration, not gated by this interval).
#define ABC80_PACING_CHECK_INTERVAL 500
#define ABC80_RENDER_INTERVAL_SEC (1.0 / 30.0)

// Real cursor-blink rate, not guessed: MAME's own abc80_v.cpp allocates
// m_blink_timer at `attotime::from_hz(XTAL(11'980'800)/2/6/64/312/16)` -
// 11,980,800 / (2*6*64*312*16) = 3.125Hz exactly, i.e. the cursor's on/off
// phase flips every 160ms (a full on-off cycle every 320ms). Used below to
// drive blink_phase in --interactive's live render loop - Milestone 7's
// own Known Gaps note ("Cursor blink still isn't live... Revisit only if
// a future milestone adds live/interactive rendering") is exactly what
// this is.
#define ABC80_BLINK_HZ 3.125

// The real periodic PIO interrupt's period/vector derivation (screen-
// scanline-driven, confirmed against this ROM's own disassembly) now lives
// as a comment on ABC80_PIO_INTERRUPT_PERIOD_TSTATES/_VECTOR themselves in
// step.h (Milestone 11's shared-step extraction), not duplicated here.

// The PIO Port A hardware-address-alias sync (main.c used to keep its own
// copy of this) now lives in keyboard.c/.h, called once per instruction
// from inside abc80_step() itself (Milestone 11's shared-step extraction) -
// see keyboard.h's own comment. abc80_keyboard_init_port_aliases() still
// needs calling once at startup, in main()'s own setup below.

// --interactive: raw terminal keyboard input + a live, periodically-
// redrawn screen, instead of batch execution to a fixed instruction cap
// followed by one final snapshot render. Everything below is gated on
// this flag and touches nothing about the existing default (batch/piped-
// input) behavior every regression test in this project already relies
// on - see main()'s own argument parsing and main loop for exactly where.
static bool interactive_mode = false;

// Mirrors cpm/emu/src/cpm.c's cpm_console_init()/cpm_console_shutdown()
// almost exactly - the same real terminal-mode problem (character-at-a-
// time input, no host-side echo since the ROM does its own via video RAM,
// real Enter arriving as a genuine 0x0D rather than being silently
// rewritten to 0x0A, Ctrl-S not being swallowed as XOFF) applies here
// identically, so the fix is the same. Deliberately duplicated rather
// than shared: this is ABC80-only code, and cpm.c is CP/M-specific by
// design (see CLAUDE.md's Architecture section on why the two targets'
// machine-specific glue stays separate from the shared core, and from
// each other).
//
// One real difference from cpm.c's version: VINTR (the character that
// normally raises SIGINT - Ctrl-C on every common terminal) is disabled
// via _POSIX_VDISABLE rather than left at its default. ISIG itself stays
// enabled, so Ctrl-\ (SIGQUIT) and Ctrl-Z (SIGTSTP) still behave
// normally - only Ctrl-C's own signal-raising is turned off. That lets a
// real Ctrl-C keystroke instead arrive as a plain 0x03 byte through
// read(), exactly like every other key, reaching abc80_keyboard_press()
// and from there the ROM's own real interrupt handler's break-combo check
// (0x83 = 0x03 with the PIO strobe bit set - see
// ABC80_PIO_INTERRUPT_PERIOD_TSTATES's own comment for that handler) -
// the genuine hardware behavior, not an emulator-only substitute. This
// closes the gap the initial --interactive milestone deliberately left
// open (see abc80/docs/ABC80_ROADMAP.md's Milestone 8 write-up): quitting
// this *tool* now needs a different key, since Ctrl-C is spoken for -
// Ctrl-\ (SIGQUIT), handled below exactly like SIGINT used to be.
static struct termios abc80_orig_termios;
static int abc80_termios_saved = 0;

static void abc80_console_shutdown(void) {
    if (abc80_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &abc80_orig_termios);
    }
}

static void abc80_console_init(void) {
    if (!isatty(STDIN_FILENO)) return;

    if (tcgetattr(STDIN_FILENO, &abc80_orig_termios) != 0) return;
    abc80_termios_saved = 1;
    atexit(abc80_console_shutdown);

    struct termios raw = abc80_orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO); // no line buffering, no local echo
    raw.c_iflag &= ~(ICRNL | INLCR | IGNCR); // real Enter must arrive as 0x0D
    raw.c_iflag &= ~IXON;                    // don't swallow Ctrl-S/Ctrl-Q
    raw.c_cc[VINTR] = _POSIX_VDISABLE;       // Ctrl-C reaches the ROM instead
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

// Set by abc80_handle_quit_signal() (registered only in --interactive
// mode, for SIGINT and SIGQUIT - see abc80_console_init()'s own comment
// for why SIGQUIT/Ctrl-\ is this tool's quit key now instead of Ctrl-C);
// checked by the main loop so it can exit through its normal end-of-main
// path (final render, run summary, and - critically - the atexit-
// registered abc80_console_shutdown() above that restores the host
// terminal). SIGINT is still handled too, not just SIGQUIT: VINTR being
// disabled only stops the *terminal driver* from raising it via Ctrl-C,
// but nothing stops an external `kill -INT` from sending it directly, and
// the default action for either signal skips atexit handlers entirely -
// which would otherwise leave a real user's shell in raw mode (no local
// echo, no line buffering) until they ran `reset`/`stty sane` themselves.
static volatile sig_atomic_t abc80_quit_requested = 0;
static volatile sig_atomic_t abc80_quit_signal = 0;

static void abc80_handle_quit_signal(int sig) {
    abc80_quit_requested = 1;
    abc80_quit_signal = sig;
}

// Non-blocking single-byte stdin read - works identically for a piped or
// interactive stdin either way: in --interactive mode, abc80_console_init()
// above has already put an interactive terminal into raw (character-at-a-
// time) mode, so this sees real keystrokes immediately rather than only
// after Enter; a piped stdin (used by this project's own regression
// testing, and by non-interactive default mode always) was never
// line-buffered in the first place. Returns -1 if nothing is available
// right now.
static int poll_stdin_byte(void) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = {0, 0};
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0) {
        return -1;
    }
    uint8_t byte;
    ssize_t n = read(STDIN_FILENO, &byte, 1);
    return (n == 1) ? byte : -1;
}

// Real ABC80 hardware has no dedicated cursor-key escape sequences (full
// VT100-style arrow keys weren't yet a settled convention in 1978) - its
// two cursor keys instead send the two adjacent ASCII control codes,
// confirmed by disassembling this ROM's own line-editor
// (`0x02BC`-`0x02CE`, the same routine keyboard.h's own comment already
// grounds Milestone 3's debounce work in): `CP 08h / JR Z,L02B9` special-
// cases byte 0x08 (Backspace/Ctrl-H) into a real destructive delete-left
// (`L035B` at 0x035B decrements the column counter, walks the line buffer
// pointer back one, and writes a literal space into video RAM at the
// vacated cell - not a guess, read directly off the ROM), while
// `CP 09h / CALL Z,L0348` special-cases byte 0x09 (Tab/Ctrl-I) into the
// non-destructive counterpart (`L0348` at 0x0348 just walks a lookahead
// pointer forward, re-displaying whatever was already there, unless it
// hits the line's own terminating CR) - i.e. cursor-right. This matches
// real ABC80 owners' own memory of the hardware exactly: the left arrow
// key doubling as backspace.
#define ABC80_LEFT_ARROW_BYTE  0x08
#define ABC80_RIGHT_ARROW_BYTE 0x09

// A modern terminal's left/right arrow keys don't send 0x08/0x09 though -
// they send a 3-byte ANSI/VT100 CSI sequence (`ESC [ D` for left, `ESC [
// C` for right). This small state machine recognizes exactly those two
// sequences as they arrive one byte at a time from poll_stdin_byte()
// (interactive terminal input only arrives this way; a piped/scripted
// stdin could send the same three bytes too, and gets the identical
// translation, for free) and rewrites them to the real ABC80 byte codes
// above before anything reaches abc80_keyboard_press() - deliberately
// narrow, not a general VT100 input parser: any other CSI sequence (e.g.
// up/down), or a lone ESC with nothing recognizable following it within
// ABC80_ESC_SEQUENCE_TIMEOUT_SEC, is simply dropped rather than
// forwarded - harmless either way, since the real ROM's own line editor
// above already silently ignores any control byte below 0x20 it doesn't
// specifically recognize (`CP 20h / JR C,L02BC`).
#define ABC80_ESC_SEQUENCE_TIMEOUT_SEC 0.05

// A host terminal sends Å/Ä/Ö/Ü/É (and lowercase) as 2-byte UTF-8
// sequences (0xC2-0xDF lead byte, 0x80-0xBF continuation) - ABC80's whole
// character set (charset.c) lives in the Latin-1 Supplement block, which
// UTF-8 always encodes in exactly 2 bytes, so no 3-/4-byte lead bytes need
// recognizing here. Mirrors the ESC/CSI state machine below: buffer the
// lead byte and wait (same timeout) for its continuation, then decode and
// look up the ABC80 byte via abc80_charset_byte_for_codepoint() - dropped
// silently if the codepoint isn't in that table, matching this function's
// existing precedent for any other unrecognized sequence.
static int poll_keyboard_byte(void) {
    static enum { ESC_NONE, ESC_SEEN, ESC_BRACKET } esc_state = ESC_NONE;
    static struct timespec esc_started;
    static int utf8_lead = -1;
    static struct timespec utf8_started;

    if (esc_state != ESC_NONE) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (double)(now.tv_sec - esc_started.tv_sec) +
                          (double)(now.tv_nsec - esc_started.tv_nsec) / 1e9;
        if (elapsed > ABC80_ESC_SEQUENCE_TIMEOUT_SEC) {
            esc_state = ESC_NONE; // gave up - a lone ESC, or a sequence
                                   // this function doesn't recognize
        }
    }

    if (utf8_lead >= 0) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (double)(now.tv_sec - utf8_started.tv_sec) +
                          (double)(now.tv_nsec - utf8_started.tv_nsec) / 1e9;
        if (elapsed > ABC80_ESC_SEQUENCE_TIMEOUT_SEC) {
            utf8_lead = -1; // gave up - a lone/malformed lead byte
        }
    }

    int b = poll_stdin_byte();

    if (esc_state == ESC_NONE) {
        if (utf8_lead >= 0) {
            if (b < 0) return -1; // mid-sequence, still waiting
            int lead = utf8_lead;
            utf8_lead = -1;
            if ((b & 0xC0) != 0x80) return -1; // not a valid continuation byte
            uint32_t codepoint = ((uint32_t)(lead & 0x1F) << 6) | (uint32_t)(b & 0x3F);
            return abc80_charset_byte_for_codepoint(codepoint);
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
        return b;
    }

    if (b < 0) return -1; // mid-sequence, still waiting for the next byte

    if (esc_state == ESC_SEEN) {
        esc_state = (b == '[') ? ESC_BRACKET : ESC_NONE;
        return (esc_state == ESC_BRACKET) ? -1 : b;
    }

    // esc_state == ESC_BRACKET
    esc_state = ESC_NONE;
    if (b == 'D') return ABC80_LEFT_ARROW_BYTE;
    if (b == 'C') return ABC80_RIGHT_ARROW_BYTE;
    return -1; // some other CSI sequence - not translated, dropped
}

// Real ABC80 ROM chip layout (see abc80/docs/ABC80_ROADMAP.md's memory map,
// grounded against MAME's src/mame/luxor/abc80.cpp): four 4Kx8 chips
// filling 0x0000-0x3FFF, in address order.
typedef struct {
    const char *filename;
    uint16_t address;
} RomImage;

static const RomImage ROM_IMAGES[] = {
    {"3506_3.a5.bin", 0x0000},
    {"3507_3.a3.bin", 0x1000},
    {"3508_3.a4.bin", 0x2000},
    {"3509_3.a2.bin", 0x3000},
};
#define ROM_CHIP_SIZE 4096
#define NUM_ROM_IMAGES (sizeof(ROM_IMAGES) / sizeof(ROM_IMAGES[0]))

// Default instruction cap for a milestone-1 debug run: no video/keyboard
// exists yet to naturally end execution, and real ABC80 ROM boot code is
// expected to settle into a busy-loop (waiting on hardware this emulator
// doesn't implement yet) rather than halt on its own. This is generous
// enough to run well past ROM init into whatever steady-state loop it
// reaches, without burning unbounded CPU on a debug run.
#define DEFAULT_MAX_INSTRUCTIONS 5000000

// Number of leading instructions to print a full PC/opcode trace for -
// enough to see the reset vector, initial jumps, and early ROM init flow
// by eye, without flooding the terminal for the full multi-million-step run.
#define TRACE_INSTRUCTIONS 100

// Where to inject a --quickload file into BOFA - BASIC's own program-
// storage pointers (cassette.h). Originally a fixed instruction-count wait
// (50,000), matching the same reasoning keyboard.c's own comment used for
// the keyboard poll loop. That stopped being safe once real periodic
// interrupts existed (see ABC80_PIO_INTERRUPT_PERIOD_TSTATES's own
// comment): this ROM's interrupt handler can pre-latch an already-pending
// keystroke into a fixed RAM cell the very first time it runs (shortly
// after boot init's own `EI`), letting a key typed at the very start of a
// run get consumed thousands of instructions earlier than the old
// ~500,000-instruction estimate assumed - a real regression caught by this
// project's own quickload regression test, not hypothetical: with the
// fixed 50,000-instruction wait, a fresh run piping `LIST\rRUN\r` straight
// after `--quickload` consumed and ran both commands before the file was
// ever injected, against BASIC's still-empty program.
//
// The first real fix attempt - inject the instant BOFA first reads
// non-zero (BASIC's boot init sets it once, right after RAM detection,
// well before `EI`) - turned out to be *provably* race-free against
// keyboard timing yet still wrong, for an unrelated reason found by the
// same regression test: shortly after `EI`, this ROM unconditionally
// re-initializes BOFA's *program* to empty (0x0A79-0x0A9B: `EOFA := BOFA`,
// a terminator byte written at BOFA itself, `HEAD := EOFA+1`) inside a
// wait loop (`0x00CC: BIT 5,(IY+15) / JR NZ,L00C6`) that re-runs it an
// unpredictable number of times - injecting right after BOFA settles
// (long before `EI`) let this later reset silently clobber the freshly
// quickloaded program before anything ever read it.
//
// Fixed for real by injecting at the one ROM address that's both
// guaranteed to run *after* that reset loop has fully exited *and*
// guaranteed to run *before* any keyboard input can possibly be read:
// 0x02AA, the entry point of the ROM's own line-reading routine (`L02AA`,
// called exactly once from the boot sequence right after the "ABC80"
// banner prints, and the sole call chain leading into the keyboard poll
// loop at 0x02F1/0x0316 - confirmed via this project's own disassembler,
// not assumed). No margin to get right or re-verify if boot timing ever
// changes again - the ordering is structural, not timed.

// Milestone 6 (ABCbus expansion, RAM sub-step): 0x4000-0xBFFF (minus the
// 0x7C00-0x7FFF video RAM window, which real hardware wires to dedicated
// onboard video RAM regardless of what's on the expansion bus - see
// video_timing.c) is the ABCbus-delegated address range. MAME's own
// abcbus_slot_device forwards every read there to abcbus_xmemfl(), whose
// default (no card attached) implementation is `return 0xff;` unconditionally
// - a fixed floating-bus value, not "whatever was last written" - confirmed
// directly from MAME's abcbus.h. This model matches that: reads in the
// always-unpopulated part of the range are forced to 0xFF via
// cpu.bus_read_hook regardless of the underlying array contents (see
// z80.h's own comment on why forcing reads alone, with writes left
// unintercepted, is sufficient and correct).
//
// --ram32k models the specific, real, well-documented 16KB RAM-expansion
// modification described in Christer Ekman's "Bygg ut din ABC 80 till 32K
// RAM" (Mikrodatorn nr 7, 1982 - the same "Mikrodatorn" RAM expansion
// MAME's own driver TODO names but doesn't implement): two banks of eight
// 4116 DRAM chips piggybacked onto the existing eight, with address-decoder
// logic modified so the *new* bank answers 0x8000-0xBFFF (32K-48K) while the
// *original* bank keeps 0xC000-0xFFFF (48K-64K) - not a separate ABCbus
// expansion card, but this is still the natural place to model it, since it
// occupies exactly the address range Milestone 1's flat-RAM simplification
// left too permissive. Verified in the article itself by reading BOFA
// (PEEK(65052)+PEEK(65053)*256): 49152 (0xC000) on the base 16K machine,
// 32768 (0x8000) once the mod is wired in - the identical check this
// project's own verification below uses. 0x4000-0x7BFF is unaffected by
// this specific mod either way (it stays floating) - real DOS/printer/IEC
// ROM cards live there instead (base address 24K/0x6000 by default, per
// ABC80-minneskort-bruksanvisning.pdf), a separate, not-yet-modeled part of
// Milestone 6.
static bool abc80_ram32k_enabled = false;

// --disk (the real committed ABC-DOS ROM at 0x6000, and this machine's
// ABC-bus port decode onto the shared card in abcbus/disk.c) lives in
// abcbus.c/abcbus.h - only the "was a DOS ROM loaded" check below still
// lives here, since this hook also handles the unrelated floating-bus/
// RAM-expansion concerns.
static uint8_t abc80_bus_read_hook(Z80 *cpu, uint16_t address, uint8_t stored_value) {
    (void)cpu;
    if (abc80_abcbus_rom_loaded() && address >= 0x6000 && address <= 0x6FFF) {
        return stored_value; // the real, loaded ABCDOS80.bin content
    }
    if (address >= 0x4000 && address <= 0x7BFF) {
        return 0xFF;
    }
    if (address >= 0x8000 && address <= 0xBFFF && !abc80_ram32k_enabled) {
        return 0xFF;
    }
    return stored_value;
}

static bool load_rom(const char *rom_dir, const RomImage *rom, uint8_t *ram) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", rom_dir, rom->filename);

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open ROM image '%s': ", path);
        perror(NULL);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size != ROM_CHIP_SIZE) {
        fprintf(stderr, "ROM image '%s' is %ld bytes, expected exactly %d\n",
                path, size, ROM_CHIP_SIZE);
        fclose(f);
        return false;
    }

    size_t read = fread(&ram[rom->address], 1, ROM_CHIP_SIZE, f);
    fclose(f);
    if (read != ROM_CHIP_SIZE) {
        fprintf(stderr, "Short read loading '%s'\n", path);
        return false;
    }

    printf("Loaded '%s' (%d bytes) at 0x%04X\n", path, ROM_CHIP_SIZE, rom->address);
    return true;
}

static void print_usage(const char *prog) {
    printf("Usage:\n");
    printf("  %s [rom_dir] [max_instructions] [--quickload FILE] [--quicksave FILE]\n", prog);
    printf("\n");
    printf("  rom_dir            Directory containing the four ROM images\n");
    printf("                     (default: resources/rom - run from inside abc80/)\n");
    printf("  max_instructions   Safety cap for this debug run\n");
    printf("                     (default: %d)\n", DEFAULT_MAX_INSTRUCTIONS);
    printf("  --quickload FILE   Inject a saved program into BASIC's program\n");
    printf("                     storage area once boot init has run (see cassette.h)\n");
    printf("  --quicksave FILE   Dump BASIC's current program storage (BOFA..EOFA)\n");
    printf("                     to FILE at the end of the run\n");
    printf("  --wav FILE         Render the SN76477 sound register's activity (port\n");
    printf("                     0x06) to a WAV file at the end of the run (see sound.h)\n");
    printf("  --ram32k           Model the real 16KB RAM-expansion mod at 0x8000-0xBFFF\n");
    printf("                     (default: base 16K machine - that range floats)\n");
    printf("  --interactive      Real keyboard input (raw terminal mode) and a live,\n");
    printf("                     continuously redrawn screen, paced to real ABC80 speed,\n");
    printf("                     instead of a one-shot batch run to max_instructions.\n");
    printf("                     Ctrl-C reaches BASIC as a real break keystroke; press\n");
    printf("                     Ctrl-\\ to exit this tool. Ignored if stdin isn't a terminal.\n");
    printf("  --disk FILE        Load the real ABC-DOS ROM at 0x6000 and fit a real\n");
    printf("                     ABC-bus floppy controller serving FILE, a 160K ABC830\n");
    printf("                     image. Without it no card is fitted and the bus floats.\n");
    printf("  --interleave N     Override the sector interleave of the disk image.\n");
    printf("                     0 means none. Two dump conventions exist for the\n");
    printf("                     same media: abc80.net's .img archive stores ABC830\n");
    printf("                     sectors physically (factor 7, the default), while\n");
    printf("                     other .dsk dumps are in logical order and need\n");
    printf("                     --interleave 0. The wrong choice shows up as a\n");
    printf("                     read error on every real file, not as a dead disk.\n");
    printf("  --dos-rom FILE     Use FILE in rom_dir as the DOS ROM instead of the\n");
    printf("                     default %s (the other real image is\n", ABC80_DEFAULT_DOS_ROM);
    printf("                     UFD80V20.bin, a different DOS driving the same card).\n");
}

int main(int argc, char *argv[]) {
    const char *rom_dir = NULL;
    long max_instructions = -1;
    const char *quickload_path = NULL;
    const char *quicksave_path = NULL;
    const char *wav_path = NULL;
    const char *disk_path = NULL;
    const char *dos_rom = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[i], "--quickload") == 0 && i + 1 < argc) {
            quickload_path = argv[++i];
        } else if (strcmp(argv[i], "--quicksave") == 0 && i + 1 < argc) {
            quicksave_path = argv[++i];
        } else if (strcmp(argv[i], "--wav") == 0 && i + 1 < argc) {
            wav_path = argv[++i];
        } else if (strcmp(argv[i], "--ram32k") == 0) {
            abc80_ram32k_enabled = true;
        } else if (strcmp(argv[i], "--interactive") == 0) {
            interactive_mode = true;
        } else if (strcmp(argv[i], "--disk") == 0 && i + 1 < argc) {
            disk_path = argv[++i];
        } else if (strcmp(argv[i], "--interleave") == 0 && i + 1 < argc) {
            char *end = NULL;
            long factor = strtol(argv[++i], &end, 10);
            if (!end || *end || factor < 0 || factor > 15) {
                fprintf(stderr, "--interleave must be 0-15\n");
                return EXIT_FAILURE;
            }
            abcbus_disk_set_interleave((unsigned)factor);
        } else if (strcmp(argv[i], "--dos-rom") == 0 && i + 1 < argc) {
            dos_rom = argv[++i];
        } else if (!rom_dir) {
            rom_dir = argv[i];
        } else if (max_instructions < 0) {
            max_instructions = atol(argv[i]);
        }
    }
    if (!rom_dir) rom_dir = "resources/rom";
    // No fixed cap in interactive mode unless the caller explicitly asked
    // for one - real-time pacing below already bounds how much CPU a live
    // session burns, so there's no need for --interactive to also end the
    // session partway through like the batch-mode default cap would.
    if (max_instructions < 0) {
        max_instructions = interactive_mode ? LONG_MAX : DEFAULT_MAX_INSTRUCTIONS;
    }

    static uint8_t ram[RAM_SIZE];

    for (size_t i = 0; i < NUM_ROM_IMAGES; i++) {
        if (!load_rom(rom_dir, &ROM_IMAGES[i], ram)) {
            return EXIT_FAILURE;
        }
    }

    // The attribute PROM isn't part of the CPU's own address space (real
    // hardware wires it directly into the video-generation logic, not
    // memory-mapped) - loaded separately here purely for the end-of-run
    // render() call below.
    static uint8_t attr_rom[ABC80_ATTR_ROM_SIZE];
    char attr_path[1024];
    snprintf(attr_path, sizeof(attr_path), "%s/attr.bin", rom_dir);
    if (!abc80_video_timing_load(attr_path, attr_rom, ABC80_ATTR_ROM_SIZE)) {
        return EXIT_FAILURE;
    }

    // Pre-fill the always-floating part of the ABCbus range (and, unless
    // --ram32k was given, the whole thing) with 0xFF - not strictly required
    // for correctness (cpu.bus_read_hook below forces every read there to
    // 0xFF regardless of the array's actual contents), but keeps a raw
    // memory dump honest about what a real floating bus would show before
    // anything ever wrote to it.
    memset(&ram[0x4000], 0xFF, 0x7C00 - 0x4000);
    if (!abc80_ram32k_enabled) {
        memset(&ram[0x8000], 0xFF, 0xC000 - 0x8000);
    }

    // --disk: load the real ABC-DOS ROM at its real base address 0x6000
    // (overwriting the floating-bus fill above for that range only - see
    // abc80_bus_read_hook's own comment) and open/create the host file
    // backing its virtual disk. Done after the floating-bus fill, not
    // before, so this ROM content survives it.
    if (disk_path) {
        if (!abc80_abcbus_init(rom_dir, dos_rom, disk_path, ram)) {
            return EXIT_FAILURE;
        }
    }

    Z80 cpu = {0};
    // cpu.memory and the `ram` argument passed to z80_execute() must be the
    // *same* buffer: opcode fetch reads the `ram` parameter directly, while
    // (HL)/(nn)/etc. operand access goes through z80_read_byte(), which
    // indexes cpu.memory instead - two separate pointers that only behave
    // consistently if they alias (see cpm/emu/src/main.c for the same
    // requirement on the CP/M side).
    cpu.memory = ram;
    cpu.bus_read_hook = abc80_bus_read_hook;
    // The ABC bus is a real device now rather than a PC-address trap, so
    // the card has to be reachable through the CPU's own I/O path. Both
    // hooks fall through to the flat io_ports[] array for every port that
    // is not the bus, which is where the PIO and the sound register still
    // live.
    cpu.io_in_hook = abc80_abcbus_io_in;
    cpu.io_out_hook = abc80_abcbus_io_out;
    z80_init_tables();
    abc80_keyboard_init_port_aliases();

    // Real Z80 SP is undefined out of reset - unlike the CP/M target (which
    // must pre-seed SP itself, since a .com file has no reset-time init
    // code of its own), the real ABC80 ROM's own reset code sets SP before
    // it's ever needed, so it's deliberately left at its zero-initialized
    // value here rather than guessed at.
    cpu.pc = 0x0000;

    if (interactive_mode) {
        abc80_console_init();
        struct sigaction sa = {0};
        sa.sa_handler = abc80_handle_quit_signal;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGQUIT, &sa, NULL);
        printf("\nStarting ABC80 ROM execution at PC=0x0000 "
               "(interactive - Ctrl-C reaches BASIC, Ctrl-\\ exits)...\n");
    } else {
        printf("\nStarting ABC80 ROM execution at PC=0x0000 (cap: %ld instructions)...\n\n",
               max_instructions);
    }

    // Coarse "did execution wander somewhere sane" check: every address
    // z80_execute() ever fetched an opcode from. A 64KB bool array is cheap
    // and simple for a debug tool - no need for anything smarter here.
    static bool visited[RAM_SIZE];
    uint16_t min_pc = 0xFFFF, max_pc = 0x0000;
    long distinct_addresses = 0;

    long instructions = 0;
    uint64_t total_cycles = 0;
    bool halted = false;
    bool quickload_done = (quickload_path == NULL);
    uint64_t next_pio_interrupt_at = ABC80_PIO_INTERRUPT_PERIOD_TSTATES;

    Abc80SoundLog sound_log;
    abc80_sound_log_init(&sound_log);

    // --interactive-only real-time pacing/rendering state. run_start_time
    // is the wall-clock origin total_cycles/ABC80_CLOCK_HZ is paced
    // against; last_render_sec is the last elapsed-real-seconds value a
    // frame was drawn at, so ABC80_RENDER_INTERVAL_SEC throttles redraws
    // independently of how often the pacing check itself runs.
    struct timespec run_start_time = {0, 0};
    double last_render_sec = -1.0;
    if (interactive_mode) {
        clock_gettime(CLOCK_MONOTONIC, &run_start_time);
    }

    while (!abc80_quit_requested && instructions < max_instructions) {
        uint16_t pc_before = cpu.pc;

        if (!interactive_mode && instructions < TRACE_INSTRUCTIONS) {
            printf("  [%6ld] PC=0x%04X  opcode=0x%02X\n", instructions, pc_before, ram[pc_before]);
        }

        if (!quickload_done && pc_before == 0x02AA) {
            abc80_cassette_quickload(ram, quickload_path);
            quickload_done = true;
        }

        if (abc80_keyboard_ready_for_next()) {
            int stdin_byte = poll_keyboard_byte();
            if (stdin_byte >= 0) {
                abc80_keyboard_press((uint8_t)stdin_byte);
            }
        }

        // The actual per-instruction step - keyboard strobe consumption,
        // sound-register write logging, the disk bypass trap, and periodic
        // PIO interrupt scheduling all happen inside here now (Milestone
        // 11's shared-step extraction - see step.h's own comment for why
        // this moved out of this loop and into a function bin/abc80-gtk
        // shares too).
        int cycles = abc80_step(&cpu, ram, &sound_log, &total_cycles, &next_pio_interrupt_at);
        instructions++;

        if (cycles < 0) {
            fprintf(stderr, "Execution halted: unimplemented opcode at PC=0x%04X\n", pc_before);
            halted = true;
            break;
        }

        // --interactive real-time pacing and live rendering (see
        // ABC80_PACING_CHECK_INTERVAL's own comment for why this only
        // runs periodically rather than every instruction). If the
        // emulated ABC80 has raced ahead of real elapsed time, sleep off
        // the difference; either way, redraw the screen at most every
        // ABC80_RENDER_INTERVAL_SEC, with a cursor blink phase computed
        // from the real 3.125Hz rate (ABC80_BLINK_HZ) rather than the
        // hardcoded "always on" main.c used before this existed.
        if (interactive_mode && (instructions % ABC80_PACING_CHECK_INTERVAL) == 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed_real = (double)(now.tv_sec - run_start_time.tv_sec) +
                                   (double)(now.tv_nsec - run_start_time.tv_nsec) / 1e9;
            double elapsed_emulated = (double)total_cycles / ABC80_CLOCK_HZ;

            if (elapsed_emulated > elapsed_real) {
                double sleep_sec = elapsed_emulated - elapsed_real;
                struct timespec req;
                req.tv_sec = (time_t)sleep_sec;
                req.tv_nsec = (long)((sleep_sec - (double)req.tv_sec) * 1e9);
                nanosleep(&req, NULL);
                elapsed_real = elapsed_emulated;
            }

            if (elapsed_real - last_render_sec >= ABC80_RENDER_INTERVAL_SEC) {
                int blink_phase = fmod(elapsed_real, 1.0 / ABC80_BLINK_HZ) < (0.5 / ABC80_BLINK_HZ);
                abc80_render_frame(stdout, &ram[0x7C00], attr_rom, blink_phase);
                last_render_sec = elapsed_real;
            }
        }

        if (!visited[pc_before]) {
            visited[pc_before] = true;
            distinct_addresses++;
            if (pc_before < min_pc) min_pc = pc_before;
            if (pc_before > max_pc) max_pc = pc_before;
        }
    }

    if (quicksave_path) {
        abc80_cassette_quicksave(ram, quicksave_path);
    }

    if (wav_path) {
        abc80_sound_render_wav(&sound_log, total_cycles, ABC80_CLOCK_HZ, wav_path);
    }

    // Video RAM (0x7C00-0x7FFF, see abc80/docs/ABC80_ROADMAP.md's memory
    // map) is directly addressable within `ram` - real hardware maps it at
    // that fixed offset, so no copy/translation is needed here, just a
    // pointer into the same flat array z80_execute() was already writing
    // through. blink_phase=1 (cursor shown, not blinked) - matches
    // non-interactive mode's original one-shot-snapshot behavior; a live
    // --interactive session already showed real blinking (ABC80_BLINK_HZ)
    // throughout the run, this is just its final frame. Printed *before*
    // the run summary below, not after: abc80_render_frame() clears the
    // screen first thing, which would otherwise wipe the summary text a
    // real interactive user is trying to read after pressing Ctrl-\.
    printf("\n--- Final video RAM render ---\n");
    abc80_render_frame(stdout, &ram[0x7C00], attr_rom, 1);

    printf("\n--- Run summary ---\n");
    const char *stop_reason = halted ? "halted on unimplemented opcode"
                             : abc80_quit_requested ?
                               (abc80_quit_signal == SIGQUIT ? "user requested exit (Ctrl-\\)"
                                                              : "user requested exit (SIGINT)")
                             : "reached instruction cap";
    printf("Instructions executed: %ld (%s)\n", instructions, stop_reason);
    printf("Total T-states:        %llu\n", (unsigned long long)total_cycles);
    printf("Final PC:              0x%04X\n", cpu.pc);
    printf("Distinct PCs visited:  %ld (range 0x%04X-0x%04X)\n",
           distinct_addresses, min_pc, max_pc);
    // BOFA (see cassette.h) is where BASIC's own boot-time RAM-size
    // detection settles the bottom of free RAM - the same value Christer
    // Ekman's magazine article itself reads via PEEK to prove the 32K mod
    // is wired in (49152/0xC000 base, 32768/0x8000 expanded). Printed here
    // as this milestone's own concrete, ROM-behavior-derived proof.
    uint16_t bofa = ram[ABC80_BOFA_ADDR] | (ram[ABC80_BOFA_ADDR + 1] << 8);
    printf("BOFA (top of ROM/detected RAM floor): 0x%04X (%s)\n", bofa,
           abc80_ram32k_enabled ? "32K RAM" : "base 16K RAM");

    return halted ? EXIT_FAILURE : EXIT_SUCCESS;
}
