#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "common.h"

static u_int8_t ram[RAM_SIZE];

// Helper to load binary files into RAM
bool load_file(const char *filename, uint16_t load_address) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("Failed to open file");
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (load_address + size > RAM_SIZE) {
        fprintf(stderr, "File too large for memory limits!\n");
        fclose(f);
        return false;
    }

    fread(&ram[load_address], 1, size, f);
    fclose(f);
    printf("Loaded '%s' (%ld bytes) at 0x%04X\n", filename, size, load_address);
    return true;
}

static void print_usage(const char *prog) {
    printf("Usage:\n");
    printf("  %s <program.com> [args...]   Run a CP/M .com file (loaded at 0x0100)\n", prog);
    printf("  %s --ccp [ccp.com]           Boot a CP/M CCP shell (default: cpm_disk/ccp.com)\n", prog);
    printf("  %s -h | --help               Show this message\n", prog);
    printf("\n");
    printf("Examples (run from inside cpm/ - see CLAUDE.md's top-level layout):\n");
    printf("  %s cpm_disk/hello.com\n", prog);
    printf("  %s emu/zexall/ZEXALL-main/zexall.com\n", prog);
    printf("  %s cpm_disk/cc.com HELLO.C\n", prog);
    printf("  %s --ccp\n", prog);
}

// Builds the CP/M command-line tail at 0x0080 (length byte) / 0x0081
// onward (raw text, not null-terminated) from any extra argv entries
// after the .com file - e.g. `bin/z80 cpm_disk/cc.com HELLO.C` (run from
// inside cpm/, see CLAUDE.md's top-level layout) needs
// "HELLO.C" to reach CC.COM the same way a real CCP would deliver it.
// Confirmed against this project's own real CCP source
// (cpm/resources/ccp/upstream/ccp.asm's bmove0/bmove1/bmove2, which copies
// starting from the first space *after* the command name): the tail
// includes that leading space as its own first byte, and the length
// byte counts it too - a bare `CC` with no arguments gets length 0.
// Real CP/M's tail buffer is at most 127 bytes (0x0081-0x00FF); longer
// input is truncated rather than corrupting whatever's above 0x00FF.
static void write_command_tail(uint8_t *ram, int argc, char *argv[], int first_arg) {
    if (first_arg >= argc) {
        ram[0x0080] = 0;
        return;
    }
    uint8_t tail[127];
    size_t len = 0;
    for (int i = first_arg; i < argc; i++) {
        if (len < sizeof(tail)) tail[len++] = ' ';
        for (const char *p = argv[i]; *p && len < sizeof(tail); p++) {
            tail[len++] = (uint8_t)toupper((unsigned char)*p);
        }
    }
    ram[0x0080] = (uint8_t)len;
    memcpy(ram + 0x0081, tail, len);
}

// Parses one host-style filename argument ("HELLO.C", "A:FOO.TXT") into
// an unopened FCB at fcb_addr the way a real CCP's own filename parser
// (fillfcb in cpm/resources/ccp/upstream/ccp.asm) would: DR, then F1-F8/
// T1-T3 space-padded and uppercased, everything else zeroed. Real
// command-line programs of this era commonly read their filename
// argument this way instead of (or as well as) the raw tail text -
// BDS C's CC.COM is the first program tested here that needs it: it
// reads straight from the default FCB at 0x005C rather than parsing the
// tail itself.
static void write_default_fcb(uint8_t *ram, uint16_t fcb_addr, const char *arg) {
    memset(ram + fcb_addr, 0, 36);
    memset(ram + fcb_addr + 1, ' ', 11); // F1-F8, T1-T3
    if (arg[0] && arg[1] == ':') {
        char drive = (char)toupper((unsigned char)arg[0]);
        if (drive >= 'A' && drive <= 'P') ram[fcb_addr] = (uint8_t)(drive - 'A' + 1);
        arg += 2;
    }
    // A real CCP expands a bare '*' into '?' for every remaining position
    // in whichever field (name or type) it appears in, rather than storing
    // it literally - confirmed against cpm/resources/ccp/upstream/ccp.asm's
    // own setname/setnam0 and setty/setty0 routines (the "must be ?'s"
    // comment there). The '*' itself consumes exactly one input character
    // no matter how many '?'s it expands to. Found via a real BDS C
    // utility, LDIR.COM, silently reporting "No (matching) members found"
    // for every library when given a "*.*" pattern - it reads the pattern
    // out of this FCB looking for '?' wildcards (as any program that reads
    // a raw FCB instead of the text tail is entitled to assume, since a
    // real CCP would already have expanded '*' before it got here), and
    // a literal '*' byte doesn't match anything.
    int i = 0;
    for (; arg[0] && arg[0] != '.' && i < 8; i++) {
        if (arg[0] == '*') {
            for (; i < 8; i++) ram[fcb_addr + 1 + i] = '?';
            arg++;
            break;
        }
        ram[fcb_addr + 1 + i] = (uint8_t)toupper((unsigned char)arg[0]);
        arg++;
    }
    while (arg[0] && arg[0] != '.') arg++; // skip any name chars past 8
    if (arg[0] == '.') {
        arg++;
        for (i = 0; arg[0] && i < 3; i++) {
            if (arg[0] == '*') {
                for (; i < 3; i++) ram[fcb_addr + 9 + i] = '?';
                arg++;
                break;
            }
            ram[fcb_addr + 9 + i] = (uint8_t)toupper((unsigned char)arg[0]);
            arg++;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return EXIT_SUCCESS;
    }

    // --ccp <path> boots a CCP (a "shell": DIR/TYPE/ERA/etc. plus running
    // other .com files by name) instead of running a single program - see
    // cpm.h's CCP_BASE/cpm_set_ccp_mode comments for how a warm boot then
    // re-enters the CCP instead of halting the emulator.
    bool ccp_boot = strcmp(argv[1], "--ccp") == 0;
    const char *test_file;
    if (ccp_boot) {
        test_file = (argc > 2) ? argv[2] : "cpm_disk/ccp.com";
    } else {
        test_file = argv[1];
    }
    uint16_t load_address = ccp_boot ? CCP_BASE : 0x0100;

    // 1. Initialize CPU & lookup tables
    Z80 cpu = {0};
    cpu.memory = ram;
    z80_init_tables();
    cpm_console_init();
    cpm_fileio_init();
    cpm_set_ccp_mode(ccp_boot);

    // 2. Load the program (or CCP) into RAM
    if (!load_file(test_file, load_address)) {
        return EXIT_FAILURE;
    }

    // In CCP mode, any command-line tail is for programs typed at the
    // CCP's own `A>` prompt interactively - the CCP builds their tail
    // itself (see write_command_tail()'s own comment), so there's
    // nothing to seed here.
    if (!ccp_boot) {
        // Real CCP also auto-parses up to the first two command-line
        // filename arguments into the default FCBs at 0x005C/0x006C -
        // see write_default_fcb()'s own comment. FCB2 (0x006C-0x008F)
        // physically overlaps the command tail buffer (0x0080 onward) -
        // a well-documented real CP/M memory-map quirk, not a bug here -
        // so which one gets written LAST wins that overlap. Real CCP
        // (cpm/resources/ccp/upstream/ccp.asm, the `move0`-then-`bmove0..3`
        // sequence around its `tran` call) builds the FCBs first and
        // writes the raw tail last, so a real command line is always
        // intact regardless of argument count; FCB2's own meaningful
        // fields (DR/name/type, 0x006C-0x0077) don't reach into the
        // overlap at all, so nothing real programs use is lost. Getting
        // this order backwards (tail first, FCBs second, as an earlier
        // version of this function did) silently truncated the tail to
        // nothing the moment a second real argument existed - found
        // compiling BDS C's L2 linker: `l2 t -d` (two arguments) produced
        // an empty tail, while `l2 t` (one argument) worked fine.
        if (argc > 2) write_default_fcb(ram, 0x005C, argv[2]);
        if (argc > 3) write_default_fcb(ram, 0x006C, argv[3]);
        write_command_tail(ram, argc, argv, 2);
    }

    // 3. Set initial registers according to CP/M standard
    cpu.pc = load_address;
    cpu.sp = 0xF000; // Set stack pointer near the top of memory - the CCP
                      // resets this itself on entry, same as a real BIOS
                      // cold boot would before ever reaching the CCP.

    // Installs the minimal BIOS jump table (JP <wboot> at 0x0000, plus
    // CONST/CONIN/CONOUT and no-op stubs for the rest) - see cpm.c's BIOS
    // comment for why real software needs this, not just a bare RET.
    cpm_bios_init(ram);

    // Standard CP/M "JP <bdos>" at 0x0005-0x0007 (see cpm.h's BDOS_ENTRY
    // comment for why the address itself matters, not just the JP
    // opcode) - check_cpm_bdos() intercepts calls to 0x0005 itself before
    // ever fetching this, so it's only ever actually reached if execution
    // somehow lands on it without going through a proper BDOS call.
    ram[0x0005] = 0xC3; // JP
    ram[0x0006] = (uint8_t)(BDOS_ENTRY & 0xFF);
    ram[0x0007] = (uint8_t)(BDOS_ENTRY >> 8);

    printf("Starting Z80 Execution Loop...\n\n");

    // 4. Main Execution Loop
    bool running = true;
    uint64_t total_cycles = 0;

    while (running) {
        // If PC reaches address 0, the program has finished or terminated.
        // Skipped in CCP mode: address 0 only holds a JP to the real WBOOT
        // vector (installed by cpm_bios_init) - that JP needs to actually
        // execute (via the z80_step() call below) so check_cpm_bios() can
        // intercept it at the vector address and redirect back into the
        // CCP. Checking pc==0 here first, before that JP ever runs, would
        // halt the whole emulator instead - a program simply doing `jp 0`
        // (hello.com's own way of returning to CP/M) never even reaches
        // the WBOOT vector otherwise.
        if (!ccp_boot && cpu.pc == 0x0000) {
            printf("\nProgram terminated normally at PC=0x0000.\n");
            break;
        }

        // Run one instruction step
        int cycles = z80_step(&cpu, ram);
        total_cycles += cycles;

        // Debug fallback: stop if CPU hits a loop or fatal unhandled opcode
        if (cycles < 0) {
            fprintf(stderr, "Execution halted at PC: 0x%04X\n", cpu.pc);
            running = false;
        }
    }

    printf("Finished. Total T-states executed: %llu\n", total_cycles);
    return EXIT_SUCCESS;
}


