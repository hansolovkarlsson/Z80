// abc80/emu/src/main.c - entry point for the Luxor ABC80 machine target:
// loads the real BASIC ROM images and runs them on the shared Z80 core
// (cpm/emu/src/z80.c/alu.c) via z80_execute() directly - never z80_step(),
// since that's CP/M's own wrapper and intercepts addresses (0x0000,
// 0x0005) that legitimately hold real ABC80 ROM code. See
// abc80/docs/ABC80_ROADMAP.md for the full memory map, sources, and what's
// still missing (sound, cassette, ABCbus).
//
// Keyboard input (Milestone 3, keyboard.c) reads stdin non-blockingly and
// feeds PIO Port A, the register the real ROM's own steady-state polling
// loop reads (confirmed via this project's own disassembler - see
// keyboard.c's top comment for the exact instructions). Renders whatever
// the ROM wrote to video RAM (0x7C00-0x7FFF, directly addressable within
// the flat `ram` array - no separate buffer needed) via render.c's
// terminal backend once the run ends, either from an unimplemented-opcode
// halt (a real bug) or the safety instruction cap (expected if nothing
// was typed - no sound/cassette exists yet to do anything else useful).

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
    printf("  %s [rom_dir] [max_instructions]\n", prog);
    printf("\n");
    printf("  rom_dir            Directory containing the four ROM images\n");
    printf("                     (default: resources/rom - run from inside abc80/)\n");
    printf("  max_instructions   Safety cap for this debug run\n");
    printf("                     (default: %d)\n", DEFAULT_MAX_INSTRUCTIONS);
}

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return EXIT_SUCCESS;
    }

    const char *rom_dir = (argc > 1) ? argv[1] : "resources/rom";
    long max_instructions = (argc > 2) ? atol(argv[2]) : DEFAULT_MAX_INSTRUCTIONS;

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

    Z80 cpu = {0};
    // cpu.memory and the `ram` argument passed to z80_execute() must be the
    // *same* buffer: opcode fetch reads the `ram` parameter directly, while
    // (HL)/(nn)/etc. operand access goes through z80_read_byte(), which
    // indexes cpu.memory instead - two separate pointers that only behave
    // consistently if they alias (see cpm/emu/src/main.c for the same
    // requirement on the CP/M side).
    cpu.memory = ram;
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

    while (instructions < max_instructions) {
        uint16_t pc_before = cpu.pc;

        if (instructions < TRACE_INSTRUCTIONS) {
            printf("  [%6ld] PC=0x%04X  opcode=0x%02X\n", instructions, pc_before, ram[pc_before]);
        }

        if (abc80_keyboard_ready_for_next()) {
            int stdin_byte = poll_stdin_byte();
            if (stdin_byte >= 0) {
                abc80_keyboard_press((uint8_t)stdin_byte);
            }
        }
        sync_pio_port_a(&cpu);

        // Edge-triggered strobe consumption, at the *specific* address
        // where this ROM actually finishes reading a key - not the first
        // instruction that merely detects the strobe is set. See
        // keyboard.h's own comment for why: this ROM's keyboard read is a
        // debounce loop requiring the strobe to stay asserted across
        // several consecutive polls (a counter this emulator can't
        // otherwise satisfy, since it decrements via a real periodic
        // interrupt this emulator doesn't generate yet - see
        // abc80/docs/ABC80_ROADMAP.md's Milestone 3 section), converging
        // on 0x0316 (`IN A,(38h); AND 7Fh; RES 7,(HL); ...`) once the
        // debounce settles. Clearing there instead of on first detection
        // lets that debounce actually complete rather than being cut off
        // after a single poll.
        bool about_to_consume_key = pc_before == 0x0316;

        int cycles = z80_execute(&cpu, ram);
        instructions++;
        if (about_to_consume_key) {
            abc80_keyboard_consumed();
        }

        if (cycles < 0) {
            fprintf(stderr, "Execution halted: unimplemented opcode at PC=0x%04X\n", pc_before);
            halted = true;
            break;
        }
        total_cycles += (uint64_t)cycles;

        if (!visited[pc_before]) {
            visited[pc_before] = true;
            distinct_addresses++;
            if (pc_before < min_pc) min_pc = pc_before;
            if (pc_before > max_pc) max_pc = pc_before;
        }
    }

    printf("\n--- Run summary ---\n");
    printf("Instructions executed: %ld%s\n", instructions,
           halted ? " (halted on unimplemented opcode)" : " (reached instruction cap)");
    printf("Total T-states:        %llu\n", (unsigned long long)total_cycles);
    printf("Final PC:              0x%04X\n", cpu.pc);
    printf("Distinct PCs visited:  %ld (range 0x%04X-0x%04X)\n",
           distinct_addresses, min_pc, max_pc);

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
