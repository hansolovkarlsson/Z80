// abc80/gtk/src/main.c - a real GTK4 window for the Luxor ABC80 machine
// target (Milestone 11, see abc80/docs/ABC80_ROADMAP.md). Unlike
// cpm/gtk/src/main.c (a thin launcher that spawns bin/z80 under a pty and
// hands it to a VteTerminal widget), this is a genuine pixel framebuffer:
// bin/abc80's own --interactive mode renders through
// abc80/emu/src/render.c, which deliberately approximates GRAPHICS mode
// (ABC80's real 2x3 block-mosaic graphics) with Unicode "Symbols for
// Legacy Computing" sextant characters, since a terminal cell can't
// address individual pixels. This binary runs the same real ROM, on the
// same shared Z80 core, but draws real pixels via Cairo instead - see the
// draw_screen() function below for the pixel-accurate TEXT/GRAPHICS
// rendering this replaces the sextant approximation with.
//
// Runs the CPU core in-process (unlike cpm/gtk/, which spawns a separate
// child process) via a single GLib timer callback that runs a bounded
// instruction batch each time it fires, then returns to GTK's own main
// loop - single-threaded, no new locking/races in a codebase that's never
// needed any (a real architectural choice made with the user before
// writing this file - see the plan preserved at
// ~/.claude/plans/mellow-cooking-parrot.md for the full reasoning).
//
// Shares abc80_step() (step.h) with bin/abc80's own --interactive loop,
// so the carefully-derived per-instruction logic (keyboard debounce,
// sound-register write detection, the disk bypass, periodic PIO
// interrupt scheduling) isn't duplicated a second time - see step.h's
// own comment.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>

#include <gtk/gtk.h>

#include "../../../z80core/z80.h"
#include "../../../abc80/emu/src/step.h"
#include "../../../abc80/emu/src/disk.h"
#include "../../../abc80/emu/src/keyboard.h"
#include "../../../abc80/emu/src/video_timing.h"
#include "../../../abc80/emu/src/chargen.h"
#include "../../../abc80/emu/src/sound.h"

// Real ABC80 Z80 clock - see abc80/emu/src/main.c's own ABC80_CLOCK_HZ
// comment for the MAME-sourced derivation. Duplicated here (a single
// constant, not logic) rather than exposed from main.c, which has no
// header of its own to export it from.
#define ABC80_CLOCK_HZ 2995200.0

// How often the GLib timer fires to advance emulation and check pacing -
// deliberately more frequent than a real display's refresh rate (a
// display frame is only actually redrawn every ABC80_RENDER_INTERVAL_SEC,
// below) so keyboard-to-screen latency and real-time pacing accuracy
// don't suffer just because redraws are throttled.
#define ABC80_GTK_TIMER_INTERVAL_MS 5
#define ABC80_RENDER_INTERVAL_SEC (1.0 / 30.0)

// Real pixel geometry - ABC80_CHARGEN_CHAR_WIDTH/_HEIGHT (chargen.h) times
// the real 40x24 screen (video_timing.h's own ABC80_SCREEN_COLS/_ROWS
// constants would collide with this file's own use of the term, so this
// project's real values - 40 and 24 - are used directly here instead of
// re-declaring/including render.h just for two integers). Scaled up for a
// modern display - the real 240x240 image would be uncomfortably small
// otherwise.
#define ABC80_GTK_COLS 40
#define ABC80_GTK_ROWS 24
#define ABC80_GTK_SCALE 3
#define ABC80_GTK_PIXEL_WIDTH (ABC80_GTK_COLS * ABC80_CHARGEN_CHAR_WIDTH)
#define ABC80_GTK_PIXEL_HEIGHT (ABC80_GTK_ROWS * ABC80_CHARGEN_CHAR_HEIGHT)

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

typedef struct {
    Z80 cpu;
    uint8_t ram[RAM_SIZE];
    uint8_t attr_rom[256];
    uint8_t chargen_rom[ABC80_CHARGEN_ROM_SIZE];
    Abc80SoundLog sound_log; // required by abc80_step()'s signature; never rendered (Milestone 11's own audio-deferred decision)
    uint64_t total_cycles;
    uint64_t next_pio_interrupt_at;
    struct timespec run_start_time;
    double last_render_sec;
    bool ram32k_enabled;
    GtkWidget *drawing_area;
    guint timer_source_id;
} AppState;

// Mirrors abc80/emu/src/main.c's own abc80_bus_read_hook() exactly (same
// floating-bus/RAM-expansion/disk-ROM-passthrough logic) - duplicated
// rather than shared since main.c has no header exposing it, and this is
// plain, low-risk data-range logic rather than the kind of subtle
// correctness code step.c/disk.c were extracted specifically to avoid
// duplicating.
static AppState *g_app_for_bus_hook = NULL;

static uint8_t abc80_gtk_bus_read_hook(Z80 *cpu, uint16_t address, uint8_t stored_value) {
    (void)cpu;
    if (abc80_disk_enabled() && address >= 0x6000 && address <= 0x6FFF) {
        return stored_value;
    }
    if (address >= 0x4000 && address <= 0x7BFF) {
        return 0xFF;
    }
    if (address >= 0x8000 && address <= 0xBFFF &&
        (!g_app_for_bus_hook || !g_app_for_bus_hook->ram32k_enabled)) {
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
    size_t n = fread(&ram[rom->address], 1, ROM_CHIP_SIZE, f);
    fclose(f);
    if (n != ROM_CHIP_SIZE) {
        fprintf(stderr, "Short read loading '%s'\n", path);
        return false;
    }
    return true;
}

// Real per-character-cell TEXT/GRAPHICS pixel rendering, replacing
// abc80/emu/src/render.c's Unicode-approximation with actual pixels -
// this is the concrete deliverable Milestone 11 exists for.
//
// GRAPHICS-mode sub-cell scanline geometry is grounded directly against
// MAME's real src/mame/luxor/abc80_v.cpp draw_character(): within the
// real 10-scanline character cell, the 2x3 block-mosaic's three rows
// split as scanlines 0-2 (top), 3-6 (middle - 4 scanlines, not 3; 10
// doesn't divide evenly by 3), 7-9 (bottom) - confirmed from MAME's own
// `if (l < 3) r0 = 0; else if (l < 7) r1 = 0; else r2 = 0;` (where `l` is
// the scanline within the cell), not assumed/evenly-divided. The 6-pixel
// width *does* split evenly (3 left-column, 3 right-column). Left-column
// bits are videoram data bits {0,2,4} (top/mid/bottom), right-column bits
// are {1,3,6} (bit 5 unused, bit 7 is the cursor flag) - the same
// extraction render.c's own `cells` byte already uses, ported here as
// three separate row-band checks instead of one combined byte since this
// renderer draws each scanline band as real filled rectangles rather than
// picking one pre-drawn glyph.
static void draw_screen(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void)area;
    AppState *app = user_data;

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_paint(cr);

    double sx = (double)width / ABC80_GTK_PIXEL_WIDTH;
    double sy = (double)height / ABC80_GTK_PIXEL_HEIGHT;
    cairo_scale(cr, sx, sy);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);

    const uint8_t *videoram = &app->ram[0x7C00];
    for (int row = 0; row < ABC80_GTK_ROWS; row++) {
        int mode = 0; // K5_LINE_END resets this at the start of every row (render.c's own comment)
        for (int col = 0; col < ABC80_GTK_COLS; col++) {
            uint16_t addr = abc80_videoram_addr((uint8_t)row, (uint8_t)col);
            uint8_t data = videoram[addr];
            uint8_t character = data & 0x7F;

            uint8_t attr = abc80_attr_lookup(app->attr_rom, character, 1);
            int blank = (attr & ABC80_J3_BLANK) != 0;
            int j = (attr & ABC80_J3_TEXT) != 0;
            int k = (attr & ABC80_J3_GRAPHICS) != 0;
            int versal = (attr & ABC80_J3_VERSAL) != 0;

            if (!j && k) mode = 0;
            if (j && !k) mode = 1;
            if (j && k) mode = !mode;

            if (!blank) continue; // background already black

            int px = col * ABC80_CHARGEN_CHAR_WIDTH;
            int py = row * ABC80_CHARGEN_CHAR_HEIGHT;

            if (mode && versal) {
                int left = (data & 0x01) != 0;
                int right = (data & 0x02) != 0;
                if (left) cairo_rectangle(cr, px, py, 3, 3);
                if (right) cairo_rectangle(cr, px + 3, py, 3, 3);
                left = (data & 0x04) != 0;
                right = (data & 0x08) != 0;
                if (left) cairo_rectangle(cr, px, py + 3, 3, 4);
                if (right) cairo_rectangle(cr, px + 3, py + 3, 3, 4);
                left = (data & 0x10) != 0;
                right = (data & 0x40) != 0;
                if (left) cairo_rectangle(cr, px, py + 7, 3, 3);
                if (right) cairo_rectangle(cr, px + 3, py + 7, 3, 3);
                cairo_fill(cr);
            } else {
                for (int r = 0; r < ABC80_CHARGEN_CHAR_HEIGHT; r++) {
                    uint8_t bits = abc80_chargen_row(app->chargen_rom, character, (uint8_t)r);
                    for (int b = 0; b < ABC80_CHARGEN_CHAR_WIDTH; b++) {
                        if (bits & (0x80 >> b)) {
                            cairo_rectangle(cr, px + b, py + r, 1, 1);
                        }
                    }
                }
                cairo_fill(cr);
            }

            if ((data & 0x80) != 0) { // cursor flag - blink not yet modeled, always shown solid
                cairo_rectangle(cr, px, py, ABC80_CHARGEN_CHAR_WIDTH, ABC80_CHARGEN_CHAR_HEIGHT);
                cairo_fill(cr);
            }
        }
    }
}

// GDK keyvals equal real ASCII/Latin-1 codepoints for every printable
// character (both follow the same X11-keysym-derived convention for the
// Latin-1 range), so most keys need no translation table at all - only
// the handful of real ABC80 control-key mappings this project's own
// --interactive mode already established (Milestone 10's real left/
// right-arrow-to-backspace/cursor-advance mapping, plus the ordinary
// Return key) need explicit handling here.
static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval,
                                guint keycode, GdkModifierType state, gpointer user_data) {
    (void)controller;
    (void)keycode;
    (void)state;
    (void)user_data;

    int ascii = -1;
    switch (keyval) {
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            ascii = 0x0D;
            break;
        case GDK_KEY_BackSpace:
        case GDK_KEY_Left:
            ascii = 0x08; // real ABC80 backspace/cursor-left (Milestone 10)
            break;
        case GDK_KEY_Right:
            ascii = 0x09; // real ABC80 non-destructive cursor-advance (Milestone 10)
            break;
        default:
            if (keyval >= 0x20 && keyval < 0x7F) {
                ascii = (int)keyval;
            }
            break;
    }
    if (ascii >= 0 && abc80_keyboard_ready_for_next()) {
        abc80_keyboard_press((uint8_t)ascii);
    }
    return TRUE;
}

// Optional scripted-input path, mirroring abc80/emu/src/main.c's own
// poll_stdin_byte() exactly (same non-blocking select()-then-read()
// pattern) - gated by isatty() so it only ever activates when stdin is
// piped/redirected, never when a real user runs this interactively (real
// input then comes exclusively through on_key_pressed()'s GDK events,
// completely unaffected). Added specifically to make GRAPHICS-mode
// rendering self-verifiable via an automated screenshot: this sandboxed
// environment has no Accessibility permission to script synthetic
// keystrokes into a real GTK window, so there was otherwise no way to type
// a test BASIC program into bin/abc80-gtk without the user's manual
// involvement.
static int poll_stdin_byte(void) {
    if (!isatty(STDIN_FILENO)) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = {0, 0};
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            uint8_t byte;
            ssize_t n = read(STDIN_FILENO, &byte, 1);
            if (n == 1) return byte;
        }
    }
    return -1;
}

static gboolean on_timer_tick(gpointer user_data) {
    AppState *app = user_data;

    if (abc80_keyboard_ready_for_next()) {
        int b = poll_stdin_byte();
        if (b >= 0) {
            abc80_keyboard_press((uint8_t)b);
        }
    }

    // The window may have been closed and its drawing area destroyed
    // since the last tick (on_window_destroy() below clears this and
    // removes this very timer, but that removal and an already-queued
    // tick can still race) - bail out immediately rather than risk
    // gtk_widget_queue_draw() on a freed widget.
    if (!app->drawing_area) {
        return G_SOURCE_REMOVE;
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed_real = (double)(now.tv_sec - app->run_start_time.tv_sec) +
                           (double)(now.tv_nsec - app->run_start_time.tv_nsec) / 1e9;
    double target_cycles = elapsed_real * ABC80_CLOCK_HZ;

    // Bounded per-tick batch: if the host stalled (a slow redraw, the
    // window being dragged, etc.), don't try to instantly replay however
    // much real time passed - cap the catch-up so one slow tick can't
    // freeze the UI trying to run millions of instructions at once. The
    // pacing loop naturally recovers over subsequent ticks instead.
    uint64_t max_cycles_this_tick = (uint64_t)(ABC80_CLOCK_HZ * 0.25);
    uint64_t stop_at = app->total_cycles + max_cycles_this_tick;

    while (app->total_cycles < (uint64_t)target_cycles && app->total_cycles < stop_at) {
        int cycles = abc80_step(&app->cpu, app->ram, &app->sound_log,
                                 &app->total_cycles, &app->next_pio_interrupt_at);
        if (cycles < 0) {
            fprintf(stderr, "Execution halted: unimplemented opcode at PC=0x%04X\n", app->cpu.pc);
            // GLib treats G_SOURCE_REMOVE as self-removal - matching that
            // here so on_window_destroy() (if the window closes later,
            // after a halt) doesn't call g_source_remove() on an ID GLib
            // has already invalidated.
            app->timer_source_id = 0;
            return G_SOURCE_REMOVE;
        }
    }

    if (elapsed_real - app->last_render_sec >= ABC80_RENDER_INTERVAL_SEC) {
        app->last_render_sec = elapsed_real;
        gtk_widget_queue_draw(app->drawing_area);
    }

    return G_SOURCE_CONTINUE;
}

// Fires when the window closes, *before* GTK finishes tearing down its
// child widgets. Removes the still-running GLib timer explicitly rather
// than leaving it to fire again against an about-to-be-freed (or already
// freed) drawing area - the real cause of the "gtk_widget_queue_draw:
// assertion 'GTK_IS_WIDGET (widget)' failed" critical this project's own
// testing found on exit before this handler existed. Clearing
// app->drawing_area to NULL too, as defense in depth: on_timer_tick()
// checks it first regardless of whether this handler's g_source_remove()
// already stopped the timer, in case any tick was already queued before
// this ran.
static void on_window_destroy(GtkWidget *window, gpointer user_data) {
    (void)window;
    AppState *app = user_data;
    if (app->timer_source_id != 0) {
        g_source_remove(app->timer_source_id);
        app->timer_source_id = 0;
    }
    app->drawing_area = NULL;
}

static void activate(GtkApplication *gtk_app, gpointer user_data) {
    AppState *app = user_data;

    GtkWidget *window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(window), "ABC80");
    gtk_window_set_default_size(GTK_WINDOW(window), ABC80_GTK_PIXEL_WIDTH * ABC80_GTK_SCALE,
                                 ABC80_GTK_PIXEL_HEIGHT * ABC80_GTK_SCALE);

    app->drawing_area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(app->drawing_area),
                                        ABC80_GTK_PIXEL_WIDTH * ABC80_GTK_SCALE);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(app->drawing_area),
                                         ABC80_GTK_PIXEL_HEIGHT * ABC80_GTK_SCALE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->drawing_area), draw_screen, app, NULL);
    gtk_window_set_child(GTK_WINDOW(window), app->drawing_area);

    GtkEventController *key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), app);
    gtk_widget_add_controller(window, key_controller);

    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), app);

    clock_gettime(CLOCK_MONOTONIC, &app->run_start_time);
    app->last_render_sec = 0.0;
    app->timer_source_id = g_timeout_add(ABC80_GTK_TIMER_INTERVAL_MS, on_timer_tick, app);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char *argv[]) {
    const char *rom_dir = "resources/rom";
    const char *disk_path = NULL;
    int arg_i = 1;
    if (arg_i < argc && argv[arg_i][0] != '-') {
        rom_dir = argv[arg_i++];
    }
    for (; arg_i < argc; arg_i++) {
        if (strcmp(argv[arg_i], "--disk") == 0 && arg_i + 1 < argc) {
            disk_path = argv[++arg_i];
        } else if (strcmp(argv[arg_i], "--ram32k") == 0) {
            // handled below, after AppState exists
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[arg_i]);
            fprintf(stderr, "Usage: %s [rom_dir] [--disk FILE] [--ram32k]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    static AppState app;
    memset(&app, 0, sizeof(app));
    g_app_for_bus_hook = &app;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ram32k") == 0) app.ram32k_enabled = true;
    }

    for (size_t i = 0; i < NUM_ROM_IMAGES; i++) {
        if (!load_rom(rom_dir, &ROM_IMAGES[i], app.ram)) {
            return EXIT_FAILURE;
        }
    }

    char chargen_path[1024];
    snprintf(chargen_path, sizeof(chargen_path), "%s/chargen.bin", rom_dir);
    if (!abc80_chargen_load(chargen_path, app.chargen_rom)) {
        return EXIT_FAILURE;
    }

    char attr_path[1024];
    snprintf(attr_path, sizeof(attr_path), "%s/attr.bin", rom_dir);
    if (!abc80_video_timing_load(attr_path, app.attr_rom, 256)) {
        return EXIT_FAILURE;
    }

    // Floating-bus fill for the ABCbus-delegated range, before --disk's
    // own DOS ROM load below so that content survives it - same ordering
    // reasoning as abc80/emu/src/main.c's own setup.
    for (int addr = 0x4000; addr <= 0xBFFF; addr++) {
        app.ram[addr] = 0xFF;
    }

    if (disk_path) {
        if (!abc80_disk_init(rom_dir, disk_path, app.ram)) {
            return EXIT_FAILURE;
        }
    }

    app.cpu.memory = app.ram;
    app.cpu.bus_read_hook = abc80_gtk_bus_read_hook;
    z80_init_tables();
    abc80_keyboard_init_port_aliases();
    abc80_sound_log_init(&app.sound_log);
    app.next_pio_interrupt_at = ABC80_PIO_INTERRUPT_PERIOD_TSTATES;

    GtkApplication *gtk_app = gtk_application_new(NULL, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), &app);
    int status = g_application_run(G_APPLICATION(gtk_app), 0, NULL);
    g_object_unref(gtk_app);
    return status;
}
