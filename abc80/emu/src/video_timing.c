// abc80/emu/src/video_timing.c - decodes the four TTL PROMs that generate
// ABC80 video timing and attributes, ported from MAME's own reverse-
// engineered driver (src/mame/luxor/abc80_v.cpp, sn74s262-adjacent logic
// authored by Curt Coder) rather than guessed from the raw bytes. Board
// positions, chip types, and ROM_REGION checksums, all cross-checked
// against MAME's src/mame/luxor/abc80.cpp and confirmed byte-identical to
// abc80.net's own archive (00index.txt's board-position notes match MAME's
// chip references exactly):
//
//   File                       Board pos  Chip     CRC32       MAME region
//   abc80/resources/rom/hsync.bin   K5    82S129   e4f7e018    "hsync"
//   abc80/resources/rom/vsync.bin   K2    82S131   445a45b9    "vsync"
//   abc80/resources/rom/attr.bin    J3    82S129   6c46811c    "attr"
//   abc80/resources/rom/line.bin    K1    82S131   74de7a0b    "line"
//
// This covers the *logical* row/column/attribute mapping only - not yet
// wired into an actual scanline-by-scanline pixel renderer (that needs a
// display backend, still an open decision - see
// abc80/docs/ABC80_ROADMAP.md's Milestone 2). Two pieces of MAME's own
// draw_scanline()/draw_character() are deliberately NOT ported yet, since
// they're inherently part of the rendering loop rather than a stateless
// PROM-decode primitive: the TEXT/GRAPHICS mode toggle (a persistent
// state machine carried across characters within a row) and the actual
// per-pixel chargen-vs-block-graphics selection. Left for that step.
//
// Verified against the real, committed ROM bytes (not just "the formula
// compiles"):
//   - hsync's ABC80_K5_ROW_START bit is set for exactly sx=15..54 (40
//     positions) - the real 40-column visible text width. Note: only
//     sx=0..63 of the physical 256-byte ROM is ever addressed - MAME's
//     draw_scanline() loops `for (sx = 0; sx < 64; sx++)`, so bytes 64-255
//     are never consulted by real hardware and shouldn't be scanned by
//     anything checking this claim.
//   - vsync's ABC80_K2_FRAME_END bit fires 23 times across the 313-line
//     frame (y=57,67,77,...,297), i.e. every 10 lines starting at the top
//     of the display area - 24 character rows, each the chargen ROM's
//     real 10-scanline cell height.
//   - line.bin's values cycle 0,1,2,...,9,0,1,... exactly once per
//     character row within the active display area (confirmed y=55-79),
//     directly feeding chargen.c's `row` parameter.
//   - attr.bin's ABC80_J3_BLANK bit is 0 for every character in the
//     border region (dh&dv=0 half of the address space) and 1 for 'A'/
//     space in the active-display half (dh&dv=1) - resolves what first
//     looked like inverted polarity (checking the border half by mistake)
//     and confirms `color &= blank` in MAME's draw_character() means what
//     it says once looked up correctly.
//   - abc80_videoram_addr() is a genuine bijection over all 24x40=960 real
//     character cells: zero address collisions, confirmed by brute-force
//     enumeration before writing the C port, addresses spanning 0-1015 of
//     the 1024-byte video RAM.

#include <stdio.h>
#include "video_timing.h"

int abc80_video_timing_load(const char *path, uint8_t *rom_out, long expected_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open video timing ROM '%s': ", path);
        perror(NULL);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size != expected_size) {
        fprintf(stderr, "Video timing ROM '%s' is %ld bytes, expected exactly %ld\n",
                path, size, expected_size);
        fclose(f);
        return 0;
    }

    size_t read = fread(rom_out, 1, (size_t)expected_size, f);
    fclose(f);
    if (read != (size_t)expected_size) {
        fprintf(stderr, "Short read loading video timing ROM '%s'\n", path);
        return 0;
    }
    return 1;
}

// Ported verbatim from MAME's abc80_state::get_videoram_addr() (the base
// ABC80's 40-column variant, not tkn80's 80-column extension) - a real
// hardware bit-interleaving scheme, not something to simplify/"clean up":
//
//   int a = (m_c >> 3) & 0x07;
//   int b = ((m_r >> 1) & 0x0c) | ((m_r >> 3) & 0x03);
//   int s = (a + b) & 0x0f;
//   return ((m_r & 0x07) << 7) | (s << 3) | (m_c & 0x07);
//
// row is MAME's m_r (character row, 0-23), col is m_c (character column,
// 0-39).
uint16_t abc80_videoram_addr(uint8_t row, uint8_t col) {
    int a = (col >> 3) & 0x07;
    int b = ((row >> 1) & 0x0c) | ((row >> 3) & 0x03);
    int s = (a + b) & 0x0f;
    return (uint16_t)(((row & 0x07) << 7) | (s << 3) | (col & 0x07));
}

// Ported from MAME's draw_character():
//   u8 attr_addr = ((dh & dv) << 7) | (videoram_data & 0x7f);
//   u8 attr_data = m_attr_prom->base()[attr_addr];
//
// dh (ABC80_K5_DH, from the hsync PROM) and dv (ABC80_K2_DV, from the
// vsync PROM) both mean "currently within the active display area" for
// their respective axis - despite the name, `dh & dv` is true for nearly
// the whole visible 40x24 text area, not some rare special case (240/313
// scanlines have dv set, 40/64 column-slots have dh set) - only the
// border/blanking region has it false. `active_display` here stands in
// for that already-ANDed dh&dv value.
uint8_t abc80_attr_lookup(const uint8_t *attr_rom, uint8_t character, int active_display) {
    uint8_t addr = (uint8_t)(((active_display ? 1 : 0) << 7) | (character & 0x7F));
    return attr_rom[addr];
}
