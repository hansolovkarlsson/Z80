// abc80/emu/src/cassette.c - "quickload"/"quicksave" cassette storage
// (Milestone 4, see abc80/docs/ABC80_ROADMAP.md), a deliberate
// simplification with real precedent: real ABC80 cassette I/O is an
// analog affair (MAME samples actual audio waveforms at 44.1kHz to decode
// tape input - see abc80_state::cassette_update() in
// src/mame/luxor/abc80.cpp), which would need real FSK-style tape-
// encoding research to emulate faithfully. MAME itself doesn't require
// that for everyday use - alongside the real cassette device, it wires up
// a QUICKLOAD_LOAD_MEMBER (abc80_state::quickload_cb) that bypasses the
// analog path entirely: read BASIC's own BOFA pointer, inject the file's
// bytes directly into RAM there, and fix up EOFA/HEAD - exactly what this
// file ports (same three RAM addresses, same "skip the file's first
// byte" convention).
//
// Honest caveat, not glossed over: MAME's quickload_cb skips the loaded
// file's first byte without the driver source explaining what it means
// (real historical .bac cassette archives may use it as a file-type
// marker, a length byte, or something else - this project could not find
// a definitive primary source for it despite real effort: abc80.net's
// archive, the TOSEC ABC80 software set, and the abc80.org mailing list
// were all checked and none had a byte-level answer). This
// implementation's own quicksave writes a fixed 0x00 placeholder there
// rather than guess at a real convention, and quickload skips whatever
// byte is present - so round-tripping through this emulator's own
// quicksave/quickload is fully verified (abc80/docs/ABC80_ROADMAP.md has
// the details), but loading a real historical .bac file downloaded from
// an archive is untested and may need that header byte's real meaning
// figured out first if it turns out to matter.

#include <stdio.h>
#include "cassette.h"

static uint16_t read_u16le(const uint8_t *ram, uint16_t addr) {
    return (uint16_t)(ram[addr] | (ram[(uint16_t)(addr + 1)] << 8));
}

static void write_u16le(uint8_t *ram, uint16_t addr, uint16_t value) {
    ram[addr] = (uint8_t)(value & 0xFF);
    ram[(uint16_t)(addr + 1)] = (uint8_t)(value >> 8);
}

int abc80_cassette_quicksave(const uint8_t *ram, const char *path) {
    uint16_t bofa = read_u16le(ram, ABC80_BOFA_ADDR);
    uint16_t eofa = read_u16le(ram, ABC80_EOFA_ADDR);

    if (eofa < bofa) {
        fprintf(stderr, "Quicksave '%s': EOFA (0x%04X) precedes BOFA (0x%04X) - nothing to save\n",
                path, eofa, bofa);
        return 0;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open '%s' for quicksave: ", path);
        perror(NULL);
        return 0;
    }

    uint8_t header = 0x00; // see this file's own top comment
    fwrite(&header, 1, 1, f);
    // [BOFA, EOFA] *inclusive* - EOFA itself holds a terminator byte
    // BASIC's RUN depends on (0x01 after typing a real program, versus
    // 0x00 left over from this emulator's own RAM zero-init), found by
    // direct comparison against a real typed-and-RUN session, not
    // guessed: LIST already worked correctly capturing only [BOFA, EOFA),
    // but RUN did not, until this terminator byte was included too.
    size_t len = (size_t)(eofa - bofa) + 1;
    size_t written = fwrite(&ram[bofa], 1, len, f);
    fclose(f);

    if (written != len) {
        fprintf(stderr, "Short write saving '%s'\n", path);
        return 0;
    }

    printf("Quicksaved %zu program bytes (0x%04X-0x%04X incl. terminator) to '%s'\n",
           len, bofa, eofa, path);
    return 1;
}

int abc80_cassette_quickload(uint8_t *ram, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open '%s' for quickload: ", path);
        perror(NULL);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 1) {
        fprintf(stderr, "Quickload '%s' is empty (need at least the 1-byte header)\n", path);
        fclose(f);
        return 0;
    }

    uint8_t header;
    if (fread(&header, 1, 1, f) != 1) {
        fprintf(stderr, "Short read loading header from '%s'\n", path);
        fclose(f);
        return 0;
    }

    long data_len = size - 1;
    uint16_t bofa = read_u16le(ram, ABC80_BOFA_ADDR);
    if ((long)bofa + data_len > 0xFFFF) {
        fprintf(stderr, "Quickload '%s' (%ld bytes) would overflow RAM from BOFA=0x%04X\n",
                path, data_len, bofa);
        fclose(f);
        return 0;
    }

    size_t read_bytes = fread(&ram[bofa], 1, (size_t)data_len, f);
    fclose(f);
    if ((long)read_bytes != data_len) {
        fprintf(stderr, "Short read loading '%s'\n", path);
        return 0;
    }

    // data_len bytes were written starting at BOFA, the last of which is
    // the terminator byte quicksave captured at EOFA itself (see its own
    // comment) - so the new EOFA is BOFA + data_len - 1, pointing *at*
    // that terminator, matching where the ROM itself leaves it after a
    // real typed program.
    uint16_t eofa = (uint16_t)(bofa + data_len - 1);
    write_u16le(ram, ABC80_EOFA_ADDR, eofa);
    write_u16le(ram, ABC80_HEAD_ADDR, (uint16_t)(eofa + 1));

    printf("Quickloaded %ld program bytes from '%s' at 0x%04X-0x%04X\n", data_len, path, bofa, eofa);
    return 1;
}
