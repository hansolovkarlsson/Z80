// abc80/emu/src/video_timing_dump.c - standalone verification tool for
// video_timing.c's PROM decode and address-mapping formulas: proves each
// claim in video_timing.c's own top comment programmatically against the
// real committed ROM bytes, rather than leaving them as narrative claims
// checked once by hand and never re-verified. PASS/FAIL convention matches
// cpm/tests/run_tests.sh.
//
// Usage: abc80-video-timing-dump [rom_dir]  (default: resources/rom, i.e.
// run from inside abc80/).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "video_timing.h"

static int failures = 0;

static void check(const char *name, int condition) {
    if (condition) {
        printf("PASS: %s\n", name);
    } else {
        printf("FAIL: %s\n", name);
        failures++;
    }
}

int main(int argc, char *argv[]) {
    const char *rom_dir = (argc > 1) ? argv[1] : "resources/rom";
    char path[1024];

    static uint8_t hsync[ABC80_HSYNC_ROM_SIZE];
    static uint8_t vsync[ABC80_VSYNC_ROM_SIZE];
    static uint8_t attr[ABC80_ATTR_ROM_SIZE];
    static uint8_t line[ABC80_LINE_ROM_SIZE];

    snprintf(path, sizeof(path), "%s/hsync.bin", rom_dir);
    if (!abc80_video_timing_load(path, hsync, ABC80_HSYNC_ROM_SIZE)) return EXIT_FAILURE;
    snprintf(path, sizeof(path), "%s/vsync.bin", rom_dir);
    if (!abc80_video_timing_load(path, vsync, ABC80_VSYNC_ROM_SIZE)) return EXIT_FAILURE;
    snprintf(path, sizeof(path), "%s/attr.bin", rom_dir);
    if (!abc80_video_timing_load(path, attr, ABC80_ATTR_ROM_SIZE)) return EXIT_FAILURE;
    snprintf(path, sizeof(path), "%s/line.bin", rom_dir);
    if (!abc80_video_timing_load(path, line, ABC80_LINE_ROM_SIZE)) return EXIT_FAILURE;

    // 40 visible character columns: hsync's ROW_START bit set for exactly
    // sx=15..54. Only sx=0..63 is ever addressed - MAME's draw_scanline()
    // loops `for (sx = 0; sx < 64; sx++)`, even though the physical PROM
    // is 256 bytes; the rest is never consulted by real hardware.
    int row_start_count = 0, row_start_first = -1, row_start_last = -1;
    for (int sx = 0; sx < 64; sx++) {
        if (hsync[sx] & ABC80_K5_ROW_START) {
            if (row_start_first < 0) row_start_first = sx;
            row_start_last = sx;
            row_start_count++;
        }
    }
    printf("  hsync ROW_START: count=%d span=%d..%d\n", row_start_count, row_start_first, row_start_last);
    check("hsync ROW_START set for exactly 40 columns (15..54)",
          row_start_count == 40 && row_start_first == 15 && row_start_last == 54);

    // 24 character rows: vsync's FRAME_END bit fires 23 times (23
    // boundaries between 24 rows), every 10 scanlines.
    int frame_end_count = 0, prev_frame_end = -1, frame_end_spacing_ok = 1;
    for (int y = 0; y < ABC80_VSYNC_ROM_SIZE; y++) {
        if (y < 313 && (vsync[y] & ABC80_K2_FRAME_END)) {
            if (prev_frame_end >= 0 && (y - prev_frame_end) != 10) frame_end_spacing_ok = 0;
            prev_frame_end = y;
            frame_end_count++;
        }
    }
    printf("  vsync FRAME_END: count=%d (implies %d character rows), 10-line spacing=%s\n",
           frame_end_count, frame_end_count + 1, frame_end_spacing_ok ? "yes" : "no");
    check("vsync FRAME_END fires 23 times, every 10 lines (24 rows)",
          frame_end_count == 23 && frame_end_spacing_ok);

    // line.bin cycles 0..9 repeatedly within the active display area.
    int line_cycle_ok = 1;
    for (int y = 55; y < 79; y++) {
        if (line[y] != (uint8_t)((y - 55 + 7) % 10)) line_cycle_ok = 0;
    }
    check("line.bin cycles 0-9 per character row (checked y=55..78)", line_cycle_ok);

    // attr.bin: BLANK bit clear in the border half, set for 'A'/space in
    // the active-display half.
    uint8_t a_border = abc80_attr_lookup(attr, 'A', 0);
    uint8_t a_active = abc80_attr_lookup(attr, 'A', 1);
    uint8_t sp_active = abc80_attr_lookup(attr, ' ', 1);
    printf("  attr('A', border)=0x%02X  attr('A', active)=0x%02X  attr(' ', active)=0x%02X\n",
           a_border, a_active, sp_active);
    check("attr BLANK bit: clear in border, set for 'A'/space in active display",
          !(a_border & ABC80_J3_BLANK) && (a_active & ABC80_J3_BLANK) && (sp_active & ABC80_J3_BLANK));

    // abc80_videoram_addr() is a bijection over all 24x40=960 real
    // character cells - no two (row, col) pairs should collide.
    static int seen[1024];
    memset(seen, 0, sizeof(seen));
    int collisions = 0, distinct = 0, max_addr = 0;
    for (int row = 0; row < 24; row++) {
        for (int col = 0; col < 40; col++) {
            uint16_t addr = abc80_videoram_addr((uint8_t)row, (uint8_t)col);
            if (addr > max_addr) max_addr = addr;
            if (seen[addr]) {
                collisions++;
            } else {
                seen[addr] = 1;
                distinct++;
            }
        }
    }
    printf("  videoram_addr: 960 cells -> %d distinct addresses (max 0x%03X), %d collisions\n",
           distinct, max_addr, collisions);
    check("videoram_addr is a bijection over all 960 character cells", collisions == 0 && distinct == 960);

    printf("\n%s\n", failures == 0 ? "All checks passed." : "Some checks FAILED.");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
