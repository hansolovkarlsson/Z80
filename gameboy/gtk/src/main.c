// A real GTK4+Cairo front end for the Game Boy core (gameboy/src/) -
// Phase 7's "real graphical front end" (see gameboy/docs/GAMEBOY_ROADMAP.md).
// Unlike cpm/gtk/src/main.c, this links the core directly into one
// binary rather than spawning a separate process: the Game Boy's output
// is a pixel framebuffer, not a text/escape-code stream, so there's no
// VteTerminal-shaped widget to hand a pty to - cpm/gtk's whole approach
// doesn't transfer. Because nothing is spawned here, the macOS
// posix_spawn/xzone crash documented in cpm/gtk/README.md (triggered by
// VTE's own child-spawn path) doesn't apply to this binary at all.
//
// Kept as its own opt-in binary (`make gameboy-gtk`), same reasoning as
// cpm/gtk: the GTK4 dependency stays out of the default `make`/`make
// test` build, and gameboy/src/main.c's existing --ppm/--wav/--input
// bring-up driver keeps working unmodified for testing.
//
// Scope of this first pass: real-time video + keyboard input only. No
// live audio output yet - gameboy/src/apu.c already generates real
// samples (main.c's --wav proves that), but playing them back live
// needs a platform audio API this project has no dependency on yet;
// left as an explicit, documented gap rather than guessed at here.

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "mmu.h"
#include "cart.h"
#include "ppu.h"
#include "timer.h"
#include "joypad.h"
#include "apu.h"

// Integer upscale factor - nearest-neighbor (see draw_frame()'s filter
// choice below), so the real 160x144 pixel grid stays sharp rather than
// blurring into a smooth 800x720ish window.
#define SCALE 4

typedef struct {
    GBCpu cpu;
    GBCart cart;
    GBPpu ppu;
    GBTimer timer;
    GBJoypad joypad;
    GBApu apu;
    GtkWidget *drawing_area;
    guint timeout_id; // step_frame()'s g_timeout_add() id - see on_window_destroy()
} GameboyApp;

static char *g_rom_path = NULL;
static GameboyApp g_app;

// DMG shade index (0=white..3=black) -> 8-bit grayscale sample, exactly
// matching gameboy/src/main.c's own shade_to_gray() - keeps this front
// end's look consistent with the --ppm dumps used for testing rather
// than inventing a separate (unconfirmed) "real DMG green" palette.
static uint8_t shade_to_gray(uint8_t shade) {
    switch (shade) {
        case 0: return 255;
        case 1: return 170;
        case 2: return 85;
        default: return 0; // 3
    }
}

static void draw_frame(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void)area;
    (void)width;
    (void)height;
    GameboyApp *app = user_data;

    cairo_surface_t *surface =
        cairo_image_surface_create(CAIRO_FORMAT_RGB24, GB_SCREEN_WIDTH, GB_SCREEN_HEIGHT);
    unsigned char *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < GB_SCREEN_HEIGHT; y++) {
        uint32_t *row = (uint32_t *)(data + y * stride);
        for (int x = 0; x < GB_SCREEN_WIDTH; x++) {
            uint8_t g = shade_to_gray(app->ppu.framebuffer[y][x]);
            row[x] = ((uint32_t)g << 16) | ((uint32_t)g << 8) | g;
        }
    }
    cairo_surface_mark_dirty(surface);

    cairo_scale(cr, SCALE, SCALE);
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_surface_destroy(surface);
}

// Runs the core until one real video frame completes (VBlank), the same
// per-tick shape gameboy/src/main.c's own --ppm loop uses. The ~16ms
// GLib timeout below is what actually paces wall-clock speed - stepping
// one frame's ~70224 T-states worth of instructions takes microseconds,
// nowhere near the timer interval, so this loop itself needs no pacing
// logic of its own.
static gboolean step_frame(gpointer user_data) {
    GameboyApp *app = user_data;
    // One frame's worth of instructions is a few thousand at most,
    // generously bounded here just so a genuinely stuck core (e.g. an
    // infinite loop with interrupts disabled) can't hang the whole
    // timer callback indefinitely.
    for (int guard = 0; guard < 500000; guard++) {
        int cycles = gb_cpu_step(&app->cpu);
        if (cycles < 0) {
            fprintf(stderr, "Illegal/unimplemented opcode at PC=0x%04X - stopping\n",
                    (unsigned)(app->cpu.pc - 1));
            return G_SOURCE_REMOVE;
        }
        gb_ppu_step(&app->ppu, &app->cpu, cycles);
        gb_timer_step(&app->timer, &app->cpu, cycles);
        gb_apu_step(&app->apu, &app->cpu, cycles);
        if (app->ppu.frame_ready) {
            app->ppu.frame_ready = 0;
            break;
        }
    }
    // Belt-and-suspenders alongside on_window_destroy()'s g_source_remove():
    // that call is what actually stops this timer from firing again once
    // the window is gone, but checking here too costs nothing and avoids
    // relying on exact widget-destruction/signal-ordering guarantees.
    if (app->drawing_area) gtk_widget_queue_draw(app->drawing_area);
    return G_SOURCE_CONTINUE;
}

// Arrows = D-pad, Z = B, X = A, Enter = Start, Right Shift = Select -
// the same layout convention several existing Game Boy emulators
// (BGB, SameBoy) default to, not invented here. Returns -1 (via
// *button) for any unmapped key, which the caller passes through
// unhandled so normal window shortcuts (e.g. closing the window) still
// work.
static int key_to_button(guint keyval, int *is_direction) {
    switch (keyval) {
        case GDK_KEY_Up:    *is_direction = 1; return GB_BUTTON_UP;
        case GDK_KEY_Down:  *is_direction = 1; return GB_BUTTON_DOWN;
        case GDK_KEY_Left:  *is_direction = 1; return GB_BUTTON_LEFT;
        case GDK_KEY_Right: *is_direction = 1; return GB_BUTTON_RIGHT;
        case GDK_KEY_z: case GDK_KEY_Z: *is_direction = 0; return GB_BUTTON_B;
        case GDK_KEY_x: case GDK_KEY_X: *is_direction = 0; return GB_BUTTON_A;
        case GDK_KEY_Return: case GDK_KEY_KP_Enter: *is_direction = 0; return GB_BUTTON_START;
        case GDK_KEY_Shift_R: *is_direction = 0; return GB_BUTTON_SELECT;
        default: return -1;
    }
}

static gboolean on_key_event(GtkEventControllerKey *controller, guint keyval, guint keycode,
                              GdkModifierType state, gpointer user_data, int pressed) {
    (void)controller;
    (void)keycode;
    (void)state;
    GameboyApp *app = user_data;
    int is_direction;
    int button = key_to_button(keyval, &is_direction);
    if (button < 0) return FALSE;
    if (is_direction) gb_joypad_set_direction(&app->joypad, &app->cpu, button, pressed);
    else gb_joypad_set_action(&app->joypad, &app->cpu, button, pressed);
    return TRUE;
}

static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode,
                                GdkModifierType state, gpointer user_data) {
    return on_key_event(controller, keyval, keycode, state, user_data, 1);
}

static gboolean on_key_released(GtkEventControllerKey *controller, guint keyval, guint keycode,
                                 GdkModifierType state, gpointer user_data) {
    return on_key_event(controller, keyval, keycode, state, user_data, 0);
}

// Without this, step_frame()'s repeating g_timeout_add() outlives the
// window: GtkApplication only quits (asynchronously, on a later main-loop
// iteration) once its last window is gone, so there's a real gap where
// the timer can fire again after gtk_window_set_child()'s drawing area
// has already been destroyed - gtk_widget_queue_draw() on that dangling
// pointer is exactly the "assertion 'GTK_IS_WIDGET (widget)' failed"
// GTK-CRITICAL a real close-the-window test reproduced. Removing the
// source synchronously on "destroy" (fired as the window and its
// children are torn down, not after) closes that gap.
static void on_window_destroy(GtkWidget *window, gpointer user_data) {
    (void)window;
    GameboyApp *app = user_data;
    if (app->timeout_id) {
        g_source_remove(app->timeout_id);
        app->timeout_id = 0;
    }
    app->drawing_area = NULL;
}

static void activate(GtkApplication *gtk_app, gpointer user_data) {
    (void)user_data;

    if (gb_cart_load(&g_app.cart, g_rom_path) != 0) {
        // gb_cart_load() already printed why - nothing more useful to
        // do than give up (matches gameboy/src/main.c's own behavior).
        exit(EXIT_FAILURE);
    }

    memset(&g_app.cpu, 0, sizeof(g_app.cpu));
    g_app.cpu.memory = calloc(1, GB_MEM_SIZE);
    g_app.cpu.cart = &g_app.cart;
    g_app.cpu.ppu = &g_app.ppu;
    g_app.cpu.timer = &g_app.timer;
    g_app.cpu.joypad = &g_app.joypad;
    g_app.cpu.apu = &g_app.apu;

    gb_ppu_reset(&g_app.ppu);
    gb_timer_reset(&g_app.timer);
    gb_joypad_reset(&g_app.joypad);
    gb_apu_reset(&g_app.apu, 44100, NULL, 0); // no live playback yet - see top-of-file comment
    gb_cpu_init_tables();
    gb_cpu_reset(&g_app.cpu);

    GtkWidget *window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(window), g_rom_path);
    gtk_window_set_default_size(GTK_WINDOW(window), GB_SCREEN_WIDTH * SCALE, GB_SCREEN_HEIGHT * SCALE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), &g_app);

    GtkWidget *area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), GB_SCREEN_WIDTH * SCALE);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), GB_SCREEN_HEIGHT * SCALE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_frame, &g_app, NULL);
    g_app.drawing_area = area;
    gtk_window_set_child(GTK_WINDOW(window), area);

    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), &g_app);
    g_signal_connect(keys, "key-released", G_CALLBACK(on_key_released), &g_app);
    gtk_widget_add_controller(window, keys);

    // ~59.7 Hz is the real DMG frame rate (pandocs' Rendering.md: 154
    // scanlines * 456 dots / 4.194304 MHz), but GLib's timeout source
    // only has millisecond granularity - 16ms is the closest round
    // number (62.5 Hz, ~4.7% fast). Not noticeable during actual play,
    // and simpler than a higher-resolution timer source for this first
    // pass; a documented approximation in the same spirit as ppu.h's
    // existing "Mode 3 is always 172 dots" simplification.
    g_app.timeout_id = g_timeout_add(16, step_frame, &g_app);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        printf("Usage:\n  %s <rom.gb>   Run a Game Boy ROM in a real GTK window\n\n", argv[0]);
        printf("Keys: arrows = D-pad, Z = B, X = A, Enter = Start, Right Shift = Select\n");
        return argc < 2 ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    g_rom_path = argv[1];

    GtkApplication *app = gtk_application_new(NULL, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    // Passing 0/NULL, not the real argc/argv - GTK's own command-line
    // handling would otherwise try to interpret the ROM path as one of
    // its own options (same reasoning as cpm/gtk/src/main.c's identical
    // g_application_run() call).
    int status = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);

    free(g_app.cpu.memory);
    gb_cart_free(&g_app.cart);
    return status;
}
