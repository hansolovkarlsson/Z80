// abc806/gtk/src/main.c - bin/abc806-gtk, a real GTK4 window for the
// Luxor ABC806.
//
// The third Cairo framebuffer app in this repository, after bin/abc80-gtk
// and bin/abc802-gtk, and the shortest of them - which is the point. It
// runs the CPU core in-process on a g_timeout_add() batch loop and shares
// abc806_step() (abc806/emu/src/step.h) with the CLI's own --interactive
// loop, so the per-instruction machine logic exists once; and the pixel
// decode is already a pure function (abc806_render_pixels() in
// emu/src/chargen.c, verified independently by bin/abc806-chargen-dump),
// so this file only turns its output into a Cairo surface.
//
// Everything therefore comes along for free and cannot drift from what
// --screenshot produces: the colour attribute plane, the RAD PROM's
// underline/flash/blank substitutions, double width, the cursor, and the
// high-resolution graphics layer composited underneath the text.
//
// Two differences from bin/abc802-gtk, both consequences of this being the
// colour machine:
//
//   - **The framebuffer is palette-indexed, not monochrome.** That app
//     maps every non-zero pixel to one amber foreground; here each pixel
//     byte is a pen 0-7 and the surface is built through the machine's own
//     eight-colour palette.
//   - **The flash phase has to be supplied.** The ABC802 blinks its cursor
//     in software through the CRTC, so pacing execution was the whole
//     implementation. The ABC806's flash is a hardware attribute with no
//     software clock behind it, so this app derives the phase from elapsed
//     real time exactly as the CLI does.
//
// No audio: this machine's only sound is a strobe the emulator does not
// sound, so like bin/abc802-gtk this app needs no SDL2 and has no threads.

#include <gtk/gtk.h>
#include <glib-unix.h>

#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../../abcbus/disk.h"
#include "../../../z80core/z80.h"
#include "../../emu/src/chargen.h"
#include "../../emu/src/memory.h"
#include "../../emu/src/ports.h"
#include "../../emu/src/render.h"
#include "../../emu/src/rtc.h"
#include "../../emu/src/step.h"
#include "../../emu/src/text.h"

#define DEFAULT_ROM_DIR "abc806/resources/rom"
#define DEFAULT_DOS_ROM "ABC806-dos.66-31.bin"

// Real ABC806 Z80 clock: 3 MHz.
#define ABC806_CLOCK_HZ 3000000.0

#define RENDER_INTERVAL_SEC (1.0 / 30.0)
#define TIMER_INTERVAL_MS 5

// The attribute plane's flash bit needs a phase and nothing in the machine
// supplies one - see this file's header. 2 Hz is the conventional rate and
// is an assumption, stated as one here and in ABC806_ROADMAP.md's gaps.
#define ABC806_FLASH_HZ 2.0

// The emulated screen is 480x250 at 80 columns, which is small on a modern
// display, so the window starts at 2x and scales to whatever the user
// resizes to afterwards.
#define DEFAULT_SCALE 2
#define CANVAS_MARGIN 24
#define NOMINAL_WIDTH 480
#define NOMINAL_HEIGHT 250

// Keystroke pacing, for the reason and at the rate the CLI uses (see
// abc806/emu/src/main.c's key_gap comment): the DART holds exactly one
// receive byte, so keys must be handed over no faster than the ROM
// consumes them. A GTK key event cannot wait in a terminal buffer the way
// a CLI keystroke can, so this app queues them itself.
#define KEY_GAP_TSTATES 300000
#define KEY_QUEUE_SIZE 64

typedef struct {
    Z80 cpu;
    uint8_t ram[RAM_SIZE];
    long long total_cycles;
    struct timespec run_start_time;
    double last_render_sec;
    double elapsed_real;      // drives the flash phase; see current_screen()

    // Pending keystrokes, oldest first. A ring buffer rather than a single
    // byte: GTK delivers key events whenever the user types, which is
    // asynchronous to the emulated machine's readiness for them.
    uint8_t key_queue[KEY_QUEUE_SIZE];
    int key_head, key_count;
    long long next_key_at;

    GtkWidget *window;
    GtkWidget *drawing_area;
    guint timer_source_id;
    bool halted;
} AppState;

// ---------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------

// The screen the pure decode wants, from the live machine. Everything but
// the flash phase comes out of abc806_current_screen(), which is the same
// snapshot --screen and --screenshot use, so the window cannot disagree
// with them about what the machine is showing.
static Abc806Screen current_screen(AppState *app) {
    bool flash_on = ((long)(app->elapsed_real * ABC806_FLASH_HZ * 2) & 1) == 0;
    Abc806Screen s;
    abc806_current_screen(&s, flash_on);
    return s;
}

// Draws the emulated screen scaled into `width` x `height`.
//
// The decode produces one palette index per pixel; this turns that into a
// Cairo image surface and blits it scaled, rather than emitting a
// cairo_rectangle() per pixel the way bin/abc80-gtk does. At 480x250 that
// would be 120,000 rectangles per frame, and one surface upload is both
// faster and simpler. CAIRO_FILTER_NEAREST keeps the pixels square when
// scaled up; the default bilinear filter would blur a 6x10 cell into mush.
static void draw_screen(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void)area;
    AppState *app = user_data;

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_paint(cr);

    if (!abc806_crtc_programmed()) return;

    Abc806Screen screen = current_screen(app);
    if (screen.columns <= 0 || screen.rows <= 0) return;

    static uint8_t pixels[ABC806_MAX_PIXELS];
    if (!abc806_render_pixels(&screen, pixels, sizeof(pixels))) return;

    int w = abc806_pixel_width(&screen);
    int h = abc806_pixel_height(&screen);

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return;
    }
    unsigned char *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);

    // The machine's own eight pens, not a colour chosen here.
    // CAIRO_FORMAT_RGB24 is a 32-bit native-endian word per pixel, and
    // abc806_palette() already returns 0xRRGGBB, so it needs no repacking.
    uint32_t pen[8];
    for (int i = 0; i < 8; i++) pen[i] = abc806_palette(i);

    cairo_surface_flush(surface);
    for (int y = 0; y < h; y++) {
        uint32_t *row = (uint32_t *)(data + (size_t)y * (size_t)stride);
        for (int x = 0; x < w; x++) {
            row[x] = pen[pixels[(size_t)y * (size_t)w + (size_t)x] & 7];
        }
    }
    cairo_surface_mark_dirty(surface);

    cairo_save(cr);
    cairo_scale(cr, (double)width / w, (double)height / h);
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);

    cairo_surface_destroy(surface);
}

// Renders the current screen to a PNG through the identical draw_screen()
// the live window uses, against an offscreen surface - so a saved image
// always matches what is displayed rather than being a second
// implementation that could drift. Used by both the File menu and the
// headless --screenshot flag.
static bool write_screenshot(AppState *app, const char *path) {
    int w = NOMINAL_WIDTH * DEFAULT_SCALE;
    int h = NOMINAL_HEIGHT * DEFAULT_SCALE;
    if (abc806_crtc_programmed()) {
        Abc806Screen screen = current_screen(app);
        int pw = abc806_pixel_width(&screen), ph = abc806_pixel_height(&screen);
        if (pw > 0 && ph > 0) { w = pw * DEFAULT_SCALE; h = ph * DEFAULT_SCALE; }
    }

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(surface);
    draw_screen(NULL, cr, w, h, app);
    bool ok = cairo_surface_write_to_png(surface, path) == CAIRO_STATUS_SUCCESS;
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return ok;
}

// ---------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------

static void queue_key(AppState *app, uint8_t byte) {
    if (app->key_count >= KEY_QUEUE_SIZE) return; // full: drop, as real hardware would
    int tail = (app->key_head + app->key_count) % KEY_QUEUE_SIZE;
    app->key_queue[tail] = byte;
    app->key_count++;
}

// GDK keyvals equal real ASCII/Latin-1 codepoints for every printable
// character, so most keys need no table. Only the handful this machine
// treats specially need naming, and they are the ones the CLI's own
// --interactive mode already uses.
//
// Ctrl-<letter> is folded to 0x01-0x1A by hand: GDK reports the plain
// letter keyval plus a separate Control modifier bit rather than
// pre-folding it the way a tty driver does. Ctrl-C matters in particular,
// reaching BASIC as a plain 0x03, which is where a break belongs.
//
// Left arrow maps to BS and the other three are dropped. On the ABC802
// that follows from a byte-by-byte sweep of its line editor, which turned
// out to have no cursor movement at all; here it is **inference** from the
// same family and year rather than a sweep of this ROM. Flagged as such in
// ABC806_ROADMAP.md's gaps too, so it is not mistaken for evidence.
static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval,
                                guint keycode, GdkModifierType state, gpointer user_data) {
    (void)controller;
    (void)keycode;
    AppState *app = user_data;

    if ((state & GDK_CONTROL_MASK) != 0) {
        guint lower = gdk_keyval_to_lower(keyval);
        if (lower >= 'a' && lower <= 'z') {
            queue_key(app, (uint8_t)(lower - 'a' + 1));
            return TRUE;
        }
    }

    switch (keyval) {
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            queue_key(app, 0x0D);
            return TRUE;
        case GDK_KEY_BackSpace:
        case GDK_KEY_Delete:
        case GDK_KEY_Left:
            queue_key(app, 0x08);
            return TRUE;
        case GDK_KEY_Escape:
            queue_key(app, 0x1B);
            return TRUE;
        case GDK_KEY_Tab:
            queue_key(app, 0x09);
            return TRUE;
        default:
            break;
    }

    // Everything else: take the Unicode codepoint the key produces and ask
    // the emulator's own charset table for the machine's byte, which is
    // how Å/Ä/Ö/Ü/É reach BASIC. Plain ASCII passes straight through.
    guint32 cp = gdk_keyval_to_unicode(keyval);
    if (cp >= 0x20 && cp < 0x7F) {
        queue_key(app, (uint8_t)cp);
        return TRUE;
    }
    if (cp >= 0x80) {
        int byte = abc806_charset_byte_for_codepoint(cp);
        if (byte >= 0) {
            queue_key(app, (uint8_t)byte);
            return TRUE;
        }
    }
    return FALSE;
}

// ---------------------------------------------------------------------
// The run loop
// ---------------------------------------------------------------------

static gboolean on_timer_tick(gpointer user_data) {
    AppState *app = user_data;

    // The window may have been closed and its drawing area destroyed since
    // the last tick - on_window_destroy() removes this timer, but that
    // removal can race an already-queued tick.
    if (!app->drawing_area) return G_SOURCE_REMOVE;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    app->elapsed_real = (double)(now.tv_sec - app->run_start_time.tv_sec) +
                        (double)(now.tv_nsec - app->run_start_time.tv_nsec) / 1e9;
    double target_cycles = app->elapsed_real * ABC806_CLOCK_HZ;

    // Bounded per-tick batch: if the host stalled (a slow redraw, the
    // window being dragged), don't replay all of that real time at once
    // and freeze the UI. Pacing recovers over subsequent ticks.
    long long stop_at = app->total_cycles + (long long)(ABC806_CLOCK_HZ * 0.25);

    while (!app->halted && app->total_cycles < (long long)target_cycles &&
           app->total_cycles < stop_at) {
        if (app->key_count > 0 && app->total_cycles >= app->next_key_at &&
            abc806_keyboard_ready() && !abc806_keyboard_busy()) {
            uint8_t byte = app->key_queue[app->key_head];
            app->key_head = (app->key_head + 1) % KEY_QUEUE_SIZE;
            app->key_count--;
            abc806_keyboard_send(byte);
            app->next_key_at = app->total_cycles + KEY_GAP_TSTATES;
        }

        if (abc806_step(&app->cpu, app->ram, &app->total_cycles) < 0) {
            fprintf(stderr, "Execution halted: unimplemented opcode at PC=0x%04X\n", app->cpu.pc);
            app->halted = true;
            // GLib treats G_SOURCE_REMOVE as self-removal, so clear the id
            // to stop on_window_destroy() removing it a second time.
            app->timer_source_id = 0;
            return G_SOURCE_REMOVE;
        }
    }

    if (app->elapsed_real - app->last_render_sec >= RENDER_INTERVAL_SEC) {
        app->last_render_sec = app->elapsed_real;
        gtk_widget_queue_draw(app->drawing_area);
    }
    return G_SOURCE_CONTINUE;
}

// SIGTERM/SIGINT reach the GTK main loop through g_unix_signal_add()
// rather than a raw signal handler, so shutdown runs on the main loop as
// an ordinary callback and tears the window down the same way closing it
// does. Without this a `kill` would terminate the process outright and a
// smoke test could not tell a clean exit from a crash - which matters,
// because a non-visual launch-and-terminate check is exactly how changes
// to this window get verified.
static gboolean on_unix_signal(gpointer user_data) {
    AppState *app = user_data;
    if (app->window) gtk_window_destroy(GTK_WINDOW(app->window));
    return G_SOURCE_REMOVE;
}

static void on_window_destroy(GtkWidget *window, gpointer user_data) {
    (void)window;
    AppState *app = user_data;
    if (app->timer_source_id) {
        g_source_remove(app->timer_source_id);
        app->timer_source_id = 0;
    }
    app->drawing_area = NULL;
}

// ---------------------------------------------------------------------
// Menu actions
// ---------------------------------------------------------------------

static void on_screenshot_response(GObject *source, GAsyncResult *res, gpointer user_data) {
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    AppState *app = user_data;
    GFile *file = gtk_file_dialog_save_finish(dialog, res, NULL);
    if (!file) return;
    char *path = g_file_get_path(file);
    if (path) {
        if (!write_screenshot(app, path)) {
            fprintf(stderr, "Failed to write screenshot '%s'\n", path);
        }
        g_free(path);
    }
    g_object_unref(file);
}

static void on_take_screenshot(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    (void)parameter;
    AppState *app = user_data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Save Screenshot");
    gtk_file_dialog_set_initial_name(dialog, "abc806-screenshot.png");
    gtk_file_dialog_save(dialog, GTK_WINDOW(app->window), NULL, on_screenshot_response, app);
}

static void on_quit_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    (void)parameter;
    AppState *app = user_data;
    if (app->window) gtk_window_destroy(GTK_WINDOW(app->window));
}

// ---------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------

typedef struct {
    AppState *app;
    const char *rom_dir;
    const char *dos_rom;
    const char *disk_paths[8];
    int disk_count;
    int columns;
    long interleave;
} StartupOptions;

// Brings the emulated machine up: ROM, hooks, clock, disks. Shared by the
// windowed path and the headless --screenshot path so neither can
// initialize the machine differently from the other.
static bool machine_init(AppState *app, const StartupOptions *opts) {
    memset(app->ram, 0, sizeof(app->ram));
    app->cpu.memory = app->ram;
    app->cpu.pc = 0x0000;
    z80_init_tables();

    if (!abc806_memory_init(&app->cpu, opts->rom_dir, opts->dos_rom)) return false;
    abc806_memory_attach(&app->cpu);
    abc806_ports_attach(&app->cpu);
    abc806_rtc_init();
    abc806_set_config(opts->columns == 80, true);
    if (opts->interleave >= 0) abcbus_disk_set_interleave((unsigned)opts->interleave);
    for (int d = 0; d < opts->disk_count; d++) {
        if (!abcbus_disk_attach_arg(opts->disk_paths[d])) return false;
    }
    return true;
}

static void activate(GtkApplication *gtk_app, gpointer user_data) {
    StartupOptions *opts = user_data;
    AppState *app = opts->app;

    GtkWidget *window = gtk_application_window_new(gtk_app);
    app->window = window;
    gtk_window_set_title(GTK_WINDOW(window), "ABC806");

    GtkWidget *layout_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    // An in-window GtkPopoverMenuBar built from a GMenu, not
    // gtk_application_set_menubar()'s native-menu-bar integration - the
    // latter needs desktop-shell-specific settings to show anything on
    // X11/Wayland, while the in-window bar looks the same everywhere. The
    // same choice the other two apps made, for the same reason.
    GMenu *menu = g_menu_new();
    GMenu *file_menu = g_menu_new();
    g_menu_append(file_menu, "Take Screenshot…", "win.screenshot");
    g_menu_append(file_menu, "Quit", "win.quit");
    g_menu_append_submenu(menu, "File", G_MENU_MODEL(file_menu));

    GtkWidget *menu_bar = gtk_popover_menu_bar_new_from_model(G_MENU_MODEL(menu));
    gtk_box_append(GTK_BOX(layout_box), menu_bar);

    static const GActionEntry actions[] = {
        {"screenshot", on_take_screenshot, NULL, NULL, NULL, {0, 0, 0}},
        {"quit", on_quit_action, NULL, NULL, NULL, {0, 0, 0}},
    };
    g_action_map_add_action_entries(G_ACTION_MAP(window), actions, G_N_ELEMENTS(actions), app);

    GtkWidget *drawing_area = gtk_drawing_area_new();
    app->drawing_area = drawing_area;
    gtk_widget_set_hexpand(drawing_area, TRUE);
    gtk_widget_set_vexpand(drawing_area, TRUE);
    gtk_widget_set_halign(drawing_area, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(drawing_area, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(drawing_area, CANVAS_MARGIN);
    gtk_widget_set_margin_end(drawing_area, CANVAS_MARGIN);
    gtk_widget_set_margin_top(drawing_area, CANVAS_MARGIN);
    gtk_widget_set_margin_bottom(drawing_area, CANVAS_MARGIN);
    gtk_widget_set_size_request(drawing_area, NOMINAL_WIDTH * DEFAULT_SCALE,
                                NOMINAL_HEIGHT * DEFAULT_SCALE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area), draw_screen, app, NULL);
    gtk_box_append(GTK_BOX(layout_box), drawing_area);

    gtk_window_set_child(GTK_WINDOW(window), layout_box);

    GtkEventController *key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), app);
    gtk_widget_add_controller(window, key_controller);

    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), app);

    clock_gettime(CLOCK_MONOTONIC, &app->run_start_time);
    app->last_render_sec = -1.0;
    app->timer_source_id = g_timeout_add(TIMER_INTERVAL_MS, on_timer_tick, app);
    g_unix_signal_add(SIGTERM, on_unix_signal, app);
    g_unix_signal_add(SIGINT, on_unix_signal, app);

    gtk_window_present(GTK_WINDOW(window));
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("A GTK4 window for the Luxor ABC806, sharing the emulator core and\n");
    printf("pixel decode with bin/abc806 - including the colour attribute plane\n");
    printf("and the high-resolution graphics layer.\n\n");
    printf("Options:\n");
    printf("  --rom-dir DIR      ROM directory (default: %s)\n", DEFAULT_ROM_DIR);
    printf("  --dos-rom FILE     DOS PROM at 0x6000 (default: %s)\n", DEFAULT_DOS_ROM);
    printf("  --disk FILE        attach FILE as a floppy image (160K = ABC830,\n");
    printf("                     640K = ABC832/834). Repeat for drives 0, 1, ...\n");
    printf("  --interleave N     override the drive's own sector interleave\n");
    printf("  --columns 40|80    characters per line (default 80)\n");
    printf("  --screenshot FILE  headless: run, render one frame to FILE, exit.\n");
    printf("                     Opens no window - this is how changes to the\n");
    printf("                     renderer are verified without a desktop.\n");
    printf("  --cycles N         with --screenshot, T-states to run first\n");
    printf("                     (default 20000000)\n");
    printf("  --type TEXT        with --screenshot, type TEXT before rendering\n");
    printf("  -h, --help         this message\n");
}

int main(int argc, char *argv[]) {
    static AppState app;
    StartupOptions opts = {&app, DEFAULT_ROM_DIR, DEFAULT_DOS_ROM, {NULL}, 0, 80, -1};
    const char *screenshot_path = NULL;
    const char *type_text = NULL;
    long long screenshot_cycles = 20000000;

    // Parsed by hand rather than through GApplication's own option
    // handling, so --screenshot can run without ever creating a
    // GtkApplication - see below.
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "--rom-dir") && i + 1 < argc) {
            opts.rom_dir = argv[++i];
        } else if (!strcmp(argv[i], "--dos-rom") && i + 1 < argc) {
            opts.dos_rom = argv[++i];
        } else if (!strcmp(argv[i], "--disk") && i + 1 < argc) {
            if (opts.disk_count < 8) opts.disk_paths[opts.disk_count++] = argv[++i];
            else { fprintf(stderr, "Too many --disk arguments\n"); return 1; }
        } else if (!strcmp(argv[i], "--interleave") && i + 1 < argc) {
            opts.interleave = atol(argv[++i]);
        } else if (!strcmp(argv[i], "--columns") && i + 1 < argc) {
            opts.columns = atoi(argv[++i]);
            if (opts.columns != 40 && opts.columns != 80) {
                fprintf(stderr, "--columns must be 40 or 80\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            screenshot_path = argv[++i];
        } else if (!strcmp(argv[i], "--cycles") && i + 1 < argc) {
            screenshot_cycles = atoll(argv[++i]);
        } else if (!strcmp(argv[i], "--type") && i + 1 < argc) {
            type_text = argv[++i];
        } else {
            fprintf(stderr, "Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!machine_init(&app, &opts)) return 1;

    // Headless render. Deliberately opens no window and starts no
    // GtkApplication: automating a screen capture against the user's real
    // desktop steals focus and switches Spaces while they are working
    // (the lesson abc80/gtk/README.md records), so this app is instead
    // made self-verifiable. It runs the machine free of any pacing, then
    // renders one frame through the identical draw_screen() the live
    // window uses.
    if (screenshot_path) {
        static uint8_t type_chars[4096];
        size_t type_len = type_text ? abc806_utf8_to_chars(type_text, type_chars, sizeof(type_chars)) : 0;
        size_t type_pos = 0;
        long long next_key_at = 0;
        bool type_gate_open = false;

        while (app.total_cycles < screenshot_cycles) {
            if (abc806_step(&app.cpu, app.ram, &app.total_cycles) < 0) {
                fprintf(stderr, "Execution halted: unimplemented opcode at PC=0x%04X\n", app.cpu.pc);
                return 1;
            }
            // The same readiness gate the CLI uses: the ROM reports the
            // keyboard ready long before it is listening, and typing at
            // T-state 0 loses the first characters. Waiting for the
            // machine to have drawn something is a real signal rather
            // than a tuned delay - see abc806/emu/src/main.c.
            if (!type_gate_open && type_pos < type_len && abc806_crtc_programmed()) {
                const uint8_t *cram = abc806_char_ram();
                for (int i = 0; i < ABC806_CHAR_RAM_SIZE; i++) {
                    uint8_t ch = cram[i] & 0x7F;
                    if (ch > 0x20 && ch < 0x7F) { type_gate_open = true; break; }
                }
            }
            if (type_gate_open && type_pos < type_len && app.total_cycles >= next_key_at &&
                abc806_keyboard_ready() && !abc806_keyboard_busy()) {
                abc806_keyboard_send(type_chars[type_pos++]);
                next_key_at = app.total_cycles + KEY_GAP_TSTATES;
            }
        }

        // The flash phase a headless render sees. Fixed rather than
        // time-derived, so a screenshot is reproducible: a test that
        // sometimes catches the dark half of the flash cycle is a test
        // that sometimes fails.
        app.elapsed_real = 0.0;

        if (!write_screenshot(&app, screenshot_path)) {
            fprintf(stderr, "Failed to write '%s'\n", screenshot_path);
            return 1;
        }
        printf("Wrote %s\n", screenshot_path);
        return 0;
    }

    GtkApplication *gtk_app = gtk_application_new("org.z80.abc806", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), &opts);
    // argc/argv are deliberately not forwarded: they have already been
    // consumed above, and GApplication would reject the ones it does not
    // recognize.
    int status = g_application_run(G_APPLICATION(gtk_app), 0, NULL);
    g_object_unref(gtk_app);
    return status;
}
