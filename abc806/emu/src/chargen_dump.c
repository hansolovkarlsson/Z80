// abc806/emu/src/chargen_dump.c - bin/abc806-chargen-dump.
//
// Renders a synthetic screen that exercises every branch of the attribute
// decode, with no CPU core involved. It exists because of this project's
// own postmortem, docs/postmortems/2026-08-28-boot-screen-cannot-validate.md:
// a machine's boot screen typically uses none of its attributes, so a
// screenshot of one can look perfect while the decode is badly broken.
//
// On the ABC806 that warning is sharper than it was on the ABC802, because
// this machine's boot screen currently contains no visible text at all -
// see ABC806_ROADMAP.md. Without this tool, milestone 2 would have nothing
// whatsoever to check the renderer against.

#include <stdio.h>
#include <string.h>

#include "chargen.h"
#include "text.h"
#include "memory.h"
#include "png.h"

#define COLUMNS 40
#define ROWS 8
#define SCANLINES 10

int main(int argc, char **argv) {
    const char *rom_dir = "abc806/resources/rom";
    const char *png_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom-dir") && i + 1 < argc) rom_dir = argv[++i];
        else if (!strcmp(argv[i], "--png") && i + 1 < argc) png_path = argv[++i];
        else {
            printf("Usage: %s [--rom-dir DIR] [--png FILE]\n", argv[0]);
            printf("\nRenders a synthetic screen exercising every attribute\n");
            printf("path, and prints it as ASCII art. No CPU core involved.\n");
            return strcmp(argv[i], "-h") && strcmp(argv[i], "--help");
        }
    }

    static uint8_t char_rom[ABC806_CHAR_ROM_SIZE];
    static uint8_t rad_prom[ABC806_RAD_PROM_SIZE];
    char path[1024];
    struct { const char *name; uint8_t *dest; size_t size; } files[] = {
        {"ABC806-char.6490243-01.bin", char_rom, sizeof char_rom},
        {"RAD.bin", rad_prom, sizeof rad_prom},
    };
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/%s", rom_dir, files[i].name);
        FILE *f = fopen(path, "rb");
        if (!f || fread(files[i].dest, 1, files[i].size, f) != files[i].size) {
            fprintf(stderr, "Cannot read '%s'\n", path);
            if (f) fclose(f);
            return 1;
        }
        fclose(f);
    }

    static uint8_t cram[0x800], aram[0x800];
    memset(cram, ' ', sizeof cram);
    memset(aram, 0, sizeof aram);

    // Each row exercises one decode path. The text says what it is, and
    // the attribute byte beside it makes it so - which means a broken
    // decode shows up as the wrong row looking wrong, not as a uniformly
    // wrong screen.
    struct { const char *text; uint8_t attr; } rows[ROWS] = {
        {"WHITE ON BLACK  normal",       0x07},  // fg 7, bg 0
        {"RED ON BLUE     colours",      0x21},  // fg 1, bg 4
        {"UNDERLINE       bit 6",        0x47},  // fg 7 + underline
        {"FLASH           bit 7",        0x87},  // fg 7 + flash
        {"BLANK           cmd 2",        0x80},  // fg==bg, cmd 2
        {"KEEP PREVIOUS   cmd 0",        0x00},  // fg==bg, cmd 0
        // 0xFF, not 0xC0. Both are command 3, but the low two bits are
        // e5/e6 - the bits that actually turn doubling on - and 0xC0
        // leaves them clear. The first version of this fixture used 0xC0
        // and so exercised command 3's attribute inheritance while never
        // doubling a pixel, which let a real x-position bug through. The
        // ROM's own sign-on uses 0xFF.
        {"DOUBLE WIDTH    cmd 3",        0xFF},  // fg==bg, cmd 3, e5+e6 set
        {"GREEN ON BLACK  colours",      0x02},  // fg 2
    };

    for (int r = 0; r < ROWS; r++) {
        const char *t = rows[r].text;
        int len = (int)strlen(t);
        for (int c = 0; c < COLUMNS; c++) {
            int idx = r * COLUMNS + c;
            cram[idx] = (uint8_t)(c < len ? t[c] : ' ');
            aram[idx] = rows[r].attr;
        }
    }

    // The double-width row needs the ROM's own pattern rather than a
    // uniform attribute. Real double width alternates a command cell
    // (0xFF: fg==bg, command 3, e5+e6 set) with a colour cell (0x07) that
    // the command reads its colours from, and duplicates the character
    // across both. Filling the row with 0xFF instead makes every cell
    // white-on-white and tests nothing - which is how the first version of
    // this fixture managed to pass while the renderer put double-width
    // cells at the wrong x.
    {
        const char *t = "ABC806 DOUBLE";
        int row = 6;
        for (int c = 0; c < COLUMNS; c++) {
            int idx = row * COLUMNS + c;
            int src = c / 2;
            cram[idx] = (uint8_t)(src < (int)strlen(t) ? t[src] : ' ');
            aram[idx] = (c & 1) ? 0x07 : 0xFF;
        }
    }

    Abc806Screen s = {
        .char_ram = cram, .attr_ram = aram,
        .char_rom = char_rom, .rad_prom = rad_prom,
        .columns = COLUMNS, .rows = ROWS, .scanlines = SCANLINES,
        .start_addr = 0, .cursor_addr = -1,
        .flash_on = false, .forty = false,
        .video_ram = NULL, .hrc = NULL, .hrs = 0,
    };

    // A synthetic high-resolution plane, because nothing the ROM draws by
    // itself exercises this and a boot screen never will. It covers the
    // four things the decode can get wrong independently:
    //
    //   - the *displayed* bank coming from HRS's low nibble (the plane is
    //     written in bank 1 and HRS says bank 1, so a renderer reading the
    //     high nibble shows an empty screen);
    //   - one byte expanding to four pixels through two hrc lookups;
    //   - the opaque bit selecting whether a dot covers text or only
    //     shows through black;
    //   - the -16 pixel offset between the plane and text column 0.
    static uint8_t plane[ABC806_VIDEO_RAM_SIZE];
    uint8_t hrc[16] = {0};
    // Entry 1: both halves opaque pen 1. Entry 2: opaque pen 2 then a
    // transparent pen 0, so one dot of the pair drops out over text.
    hrc[1] = 0x99;   // 1001 1001 -> opaque pen 1, opaque pen 1
    hrc[2] = 0xA0;   // 1010 0000 -> opaque pen 2, transparent pen 0
    const uint32_t bank1 = 1u << 15;
    for (int x = 0; x < 24; x++)
        plane[bank1 + 4 * ABC806_HR_BYTES_PER_ROW + x] = 0x11;   // row 4
    for (int x = 0; x < 24; x++)
        plane[bank1 + 6 * ABC806_HR_BYTES_PER_ROW + x] = 0x22;   // row 6
    s.video_ram = plane;
    s.hrc = hrc;
    s.hrs = 0x01;    // displayed bank 1; CPU bank (high nibble) deliberately 0

    static uint8_t pixels[ABC806_MAX_PIXELS];
    if (!abc806_render_pixels(&s, pixels, sizeof pixels)) {
        fprintf(stderr, "Render failed\n");
        return 1;
    }
    int w = abc806_pixel_width(&s), h = abc806_pixel_height(&s);

    // ASCII art, one character per pixel, digit = palette index. A diff
    // against a committed fixture then catches any change to the decode.
    printf("ABC806 chargen: %dx%d, %d columns x %d rows\n", w, h, COLUMNS, ROWS);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t p = pixels[y * w + x];
            putchar(p ? (char)('0' + p) : '.');
        }
        putchar('\n');
    }

    // The same screen as characters, and as the ANSI frame --interactive
    // draws. ESC is printed as `\e` so the fixture stays a diffable text
    // file; every other byte is verbatim, so the committed colour codes
    // are the ones a terminal would actually receive.
    printf("\n--- text ---\n");
    abc806_text_screen(stdout, &s);
    printf("\n--- ansi frame ---\n");
    {
        char buf[65536];
        FILE *mem = fmemopen(buf, sizeof buf, "w");
        if (!mem) { fprintf(stderr, "fmemopen failed\n"); return 1; }
        abc806_ansi_frame(mem, &s);
        long n = ftell(mem);
        fclose(mem);
        for (long i = 0; i < n; i++) {
            if (buf[i] == 0x1B) printf("\\e");
            else putchar(buf[i]);
        }
    }

    if (png_path) {
        uint32_t pal[8];
        for (int i = 0; i < 8; i++) pal[i] = abc806_palette(i);
        if (!abc806_write_png(png_path, pixels, w, h, pal, 8)) {
            fprintf(stderr, "Could not write '%s'\n", png_path);
            return 1;
        }
        printf("Wrote %s\n", png_path);
    }
    return 0;
}
