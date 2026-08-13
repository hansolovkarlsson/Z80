// abc80/emu/src/main.c - entry point for the Luxor ABC80 machine target:
// loads the real BASIC ROM images and runs them on the shared Z80 core
// (cpm/emu/src/z80.c/alu.c) via z80_execute() directly - never z80_step(),
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

#include "../../../cpm/emu/src/z80.h"
#include "render.h"
#include "video_timing.h"
#include "keyboard.h"
#include "cassette.h"
#include "sound.h"

// Real ABC80 Z80 clock (11.9808 MHz crystal / 2 / 2 - see MAME's
// abc80_state::abc80_common(): `Z80(config, m_maincpu,
// XTAL(11'980'800)/2/2)`), needed to convert T-states into real seconds
// for sound.c's WAV rendering.
#define ABC80_CLOCK_HZ 2995200.0

// Real ABC80 hardware ties the Z80 PIO's Port A ASTB (strobe) pin to the
// video scanline clock (MAME's abc80_state::scanline_tick(), toggled once
// per scanline) rather than to a real keyboard handshake signal - a
// hardware trick that turns Port A's normal "input-mode strobe rising edge
// -> interrupt" behavior into a genuine periodic timer interrupt, entirely
// independent of actual keystrokes. Derived, not guessed: real screen
// timing (MAME's abc80.h: ABC80_HTOTAL=384 pixels, pixel clock
// XTAL(11'980'800)/2 = 5,990,400Hz) gives a 15,600Hz line rate
// (5,990,400/384); at the real 2,995,200Hz CPU clock (ABC80_CLOCK_HZ
// above), that's exactly 192 T-states per scanline. Since scanline_tick()
// *toggles* the strobe on every call rather than pulsing it once per line,
// only every *other* call is a rising edge - the actual interrupt-
// triggering event (MAME's z80pio_device::pio_port::strobe(), MODE_INPUT
// case) - so real interrupts arrive every 384 T-states (2 scanlines), a
// 7800Hz rate.
//
// Confirmed against this ROM's own real disassembly (bin/z80dasm), not
// just MAME's driver comment: boot init (0x0068-0x00C5, see main()'s own
// trace at TRACE_INSTRUCTIONS) sets IM 2, I=0 (`LD I,A` with A=0 at
// 0x008C), and writes exactly one Z80 PIO Port A control sequence via
// `OUT (39h),A` three times (0x39 & 0x17 == 0x11, Port A control under the
// 0x17 hardware address mask - see video_timing.c's port-map comment):
// 0x34 (interrupt vector - MAME's z80pio.cpp control_write() treats any
// control-port byte with bit0=0 as a vector load, regardless of mode),
// 0xB7 (interrupt control word: D7 enable=1, D4 mask-follows=1), 0x7F (the
// mask byte the mask-follows bit above requires next). With I=0 and
// vector=0x34, the real IM2 vector-table entry is at 0x0034; this ROM's
// own bytes there (0x1E 0x03, little-endian) point to 0x031E - a real
// interrupt handler, confirmed by its own RETI at 0x0336. It reads Port A
// directly, checks for a Ctrl-C-style break combo (0x83), and - regardless
// of that check - unconditionally reloads a fixed value (0x46) into
// 0xFDF7 (IX+4 in the keyboard poll loop's own IX=0xFDF3 base, confirmed
// at 0x02A5) before EI/RETI: exactly the debounce-counter refresh
// Milestone 3's own keyboard.h comment already identified from indirect
// evidence (a periodic refresh this emulator couldn't yet supply), now
// grounded directly by finding and reading the real handler. Milestone 3's
// own PC==0x0316 strobe-consumption hook (main.c's own `about_to_consume_
// key` below) is unaffected by this and still needed regardless: it tracks
// *this emulator's own* host-keystroke queue, not the ROM's internal
// debounce state, and 0x0316 remains the single real address where the
// ROM's poll loop - via either its interrupt-driven fast path or its
// direct-polling decrement fallback - genuinely finishes consuming a key,
// confirmed by both paths in the disassembly funneling through it.
#define ABC80_PIO_INTERRUPT_PERIOD_TSTATES 384
#define ABC80_PIO_INTERRUPT_VECTOR 0x34

// Every I/O port address that aliases PIO Port A's data register under
// ABC80's real hardware address decoding (MAME's `map.global_mask(0x17)` -
// see video_timing.c's port-map comment): only bits 0,1,2,4 of the port
// address are actually wired to anything, so any port P with
// (P & 0x17) == 0x10 reads/writes the identical register - real software
// (this ROM included, via `IN A,(38h)`) can and does address it through
// more than one of these. z80_io_in()/z80_io_out() (cpm/emu/src/z80.c) are
// a plain flat 256-entry array with no device logic of their own - by
// design, see that file's own comment - so this machine layer has to keep
// every alias in sync itself rather than the CPU core knowing anything
// about the mask.
static uint8_t pio_port_a_aliases[16];
static int num_pio_port_a_aliases = 0;

static void init_pio_port_a_aliases(void) {
    for (int p = 0; p < 256; p++) {
        if ((p & 0x17) == 0x10) {
            pio_port_a_aliases[num_pio_port_a_aliases++] = (uint8_t)p;
        }
    }
}

static void sync_pio_port_a(Z80 *cpu) {
    uint8_t value = abc80_keyboard_port_a();
    for (int i = 0; i < num_pio_port_a_aliases; i++) {
        cpu->io_ports[pio_port_a_aliases[i]] = value;
    }
}

// Non-blocking single-byte stdin read (works identically for a piped or
// interactive stdin - no termios raw-mode setup here yet, unlike
// cpm/emu/src/cpm.c's console handling, so an interactive terminal will
// still line-buffer until Enter; piped input, used by this project's own
// regression testing, is unaffected by that). Returns -1 if nothing is
// available right now.
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

static uint8_t abc80_bus_read_hook(Z80 *cpu, uint16_t address, uint8_t stored_value) {
    (void)cpu;
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
}

int main(int argc, char *argv[]) {
    const char *rom_dir = NULL;
    long max_instructions = -1;
    const char *quickload_path = NULL;
    const char *quicksave_path = NULL;
    const char *wav_path = NULL;

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
        } else if (!rom_dir) {
            rom_dir = argv[i];
        } else if (max_instructions < 0) {
            max_instructions = atol(argv[i]);
        }
    }
    if (!rom_dir) rom_dir = "resources/rom";
    if (max_instructions < 0) max_instructions = DEFAULT_MAX_INSTRUCTIONS;

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

    Z80 cpu = {0};
    // cpu.memory and the `ram` argument passed to z80_execute() must be the
    // *same* buffer: opcode fetch reads the `ram` parameter directly, while
    // (HL)/(nn)/etc. operand access goes through z80_read_byte(), which
    // indexes cpu.memory instead - two separate pointers that only behave
    // consistently if they alias (see cpm/emu/src/main.c for the same
    // requirement on the CP/M side).
    cpu.memory = ram;
    cpu.bus_read_hook = abc80_bus_read_hook;
    z80_init_tables();
    init_pio_port_a_aliases();

    // Real Z80 SP is undefined out of reset - unlike the CP/M target (which
    // must pre-seed SP itself, since a .com file has no reset-time init
    // code of its own), the real ABC80 ROM's own reset code sets SP before
    // it's ever needed, so it's deliberately left at its zero-initialized
    // value here rather than guessed at.
    cpu.pc = 0x0000;

    printf("\nStarting ABC80 ROM execution at PC=0x0000 (cap: %ld instructions)...\n\n",
           max_instructions);

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

    while (instructions < max_instructions) {
        uint16_t pc_before = cpu.pc;

        if (instructions < TRACE_INSTRUCTIONS) {
            printf("  [%6ld] PC=0x%04X  opcode=0x%02X\n", instructions, pc_before, ram[pc_before]);
        }

        if (!quickload_done && pc_before == 0x02AA) {
            abc80_cassette_quickload(ram, quickload_path);
            quickload_done = true;
        }

        if (abc80_keyboard_ready_for_next()) {
            int stdin_byte = poll_stdin_byte();
            if (stdin_byte >= 0) {
                abc80_keyboard_press((uint8_t)stdin_byte);
            }
        }
        sync_pio_port_a(&cpu);

        // Whether the *upcoming* z80_execute() call will actually fetch
        // and run the opcode at pc_before, or instead divert into
        // interrupt-acceptance and leave pc_before's real instruction
        // un-executed until some later call reaches this same PC again -
        // mirrors z80_execute()'s own acceptance check (z80.c) exactly, so
        // it has to be evaluated *before* that call, from the same CPU
        // state. Needed once real periodic interrupts existed (see
        // ABC80_PIO_INTERRUPT_PERIOD_TSTATES's own comment): every
        // `ram[pc_before]`-based prediction below silently assumed
        // z80_execute() always executes that exact instruction, which
        // stopped being true the moment interrupts could intercept a step
        // instead - caught by this project's own regression testing, not
        // hypothetical: typing `10 PRINT 1+1` after enabling the interrupt
        // dropped the "N" and produced a real `ERR 11`, from a spurious
        // abc80_keyboard_consumed() firing on a step that predicted
        // PC==0x0316 but actually served an interrupt that instruction
        // boundary instead, releasing the next host keystroke too early.
        bool interrupt_will_intercept_this_step =
            cpu.nmi_pending || (cpu.int_pending && cpu.iff1 && !cpu.ei_delay);

        // Edge-triggered strobe consumption, at the *specific* address
        // where this ROM actually finishes reading a key - not the first
        // instruction that merely detects the strobe is set. See
        // keyboard.h's own comment for why: this ROM's keyboard read is a
        // debounce loop requiring the strobe to stay asserted across
        // several consecutive polls, converging on 0x0316
        // (`IN A,(38h); AND 7Fh; RES 7,(HL); ...`) once the debounce
        // settles - now via either the real periodic interrupt's fast
        // path or the direct-polling decrement fallback (see
        // ABC80_PIO_INTERRUPT_PERIOD_TSTATES's own comment for the full
        // disassembly-grounded story of both paths). Clearing there
        // instead of on first detection lets that debounce actually
        // complete rather than being cut off after a single poll; gated
        // by interrupt_will_intercept_this_step above so an intercepted
        // step (pc_before==0x0316 predicted, but not actually executed
        // this call) doesn't fire early.
        bool about_to_consume_key =
            pc_before == 0x0316 && !interrupt_will_intercept_this_step;

        // OUT (n),A (0xD3) targeting the SN76477 sound register alias
        // (see sound.c's own top comment for the port-0x06 bit layout;
        // masked by the same 0x17 hardware address decode as the PIO -
        // video_timing.c's port-map comment). A doesn't change across
        // OUT, so it's still readable after z80_execute() runs.
        // OUT targeting the SN76477 sound register alias (port 0x06,
        // masked - see sound.c's own top comment). Two real opcode forms
        // both matter here, confirmed by tracing actual BASIC-compiled
        // code, not assumed: `OUT (n),A` (0xD3, immediate port) for
        // hand-written assembly, but BASIC's own compiled `OUT port,value`
        // statement uses the ED-prefixed register-indirect form instead
        // (port from BC, value from whichever of B/C/D/E/H/L/A the
        // instruction names - BASIC's own generated code was traced
        // using `OUT (C),L`) since a general two-expression BASIC
        // statement can't rely on the port being a compile-time constant
        // the way `OUT (n),A` requires.
        // Same interrupt-interception hazard as about_to_consume_key above
        // applies here too: gated by interrupt_will_intercept_this_step so
        // a step that only *predicted* an OUT-to-sound-port at pc_before,
        // but actually diverted into an interrupt instead, doesn't log a
        // phantom sound-register write.
        bool about_to_write_sound = false;
        uint8_t sound_out_value = 0;
        if (interrupt_will_intercept_this_step) {
            // handled: leave about_to_write_sound false
        } else if (ram[pc_before] == 0xD3 && (ram[(uint16_t)(pc_before + 1)] & 0x17) == 0x06) {
            about_to_write_sound = true;
            sound_out_value = cpu.a;
        } else if (ram[pc_before] == 0xED && (ram[(uint16_t)(pc_before + 1)] & 0xC7) == 0x41 &&
                   (cpu.c & 0x17) == 0x06) {
            about_to_write_sound = true;
            switch ((ram[(uint16_t)(pc_before + 1)] >> 3) & 0x07) {
                case 0: sound_out_value = cpu.b; break;
                case 1: sound_out_value = cpu.c; break;
                case 2: sound_out_value = cpu.d; break;
                case 3: sound_out_value = cpu.e; break;
                case 4: sound_out_value = cpu.h; break;
                case 5: sound_out_value = cpu.l; break;
                case 6: sound_out_value = 0; break; // undocumented OUT (C),0
                case 7: sound_out_value = cpu.a; break;
            }
        }

        int cycles = z80_execute(&cpu, ram);
        instructions++;
        if (about_to_consume_key) {
            abc80_keyboard_consumed();
        }
        if (about_to_write_sound) {
            abc80_sound_write(&sound_log, total_cycles, sound_out_value);
        }

        if (cycles < 0) {
            fprintf(stderr, "Execution halted: unimplemented opcode at PC=0x%04X\n", pc_before);
            halted = true;
            break;
        }
        total_cycles += (uint64_t)cycles;

        // Real hardware's video-scanline-driven PIO interrupt (see this
        // constant's own top comment) runs unconditionally from power-on,
        // regardless of whether the ROM has enabled interrupts yet -
        // z80_request_int() is level-held (cpu.int_pending stays set until
        // actually serviced), so requesting it early/often is harmless and
        // correctly mirrors real hardware: the CPU simply won't service it
        // until its own IFF1 allows (the ROM's `EI` at 0x00C5). A `while`
        // (not `if`) keeps the schedule from drifting even in the
        // impossible case of a single instruction taking longer than one
        // period.
        while (total_cycles >= next_pio_interrupt_at) {
            z80_request_int(&cpu, ABC80_PIO_INTERRUPT_VECTOR);
            next_pio_interrupt_at += ABC80_PIO_INTERRUPT_PERIOD_TSTATES;
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

    printf("\n--- Run summary ---\n");
    printf("Instructions executed: %ld%s\n", instructions,
           halted ? " (halted on unimplemented opcode)" : " (reached instruction cap)");
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

    // Video RAM (0x7C00-0x7FFF, see abc80/docs/ABC80_ROADMAP.md's memory
    // map) is directly addressable within `ram` - real hardware maps it at
    // that fixed offset, so no copy/translation is needed here, just a
    // pointer into the same flat array z80_execute() was already writing
    // through. blink_phase=1 (cursor shown, not blinked) - a real blink
    // needs a live/periodic render loop, out of scope for this one-shot
    // end-of-run snapshot.
    printf("\n--- Final video RAM render ---\n");
    abc80_render_frame(stdout, &ram[0x7C00], attr_rom, 1);

    return halted ? EXIT_FAILURE : EXIT_SUCCESS;
}
