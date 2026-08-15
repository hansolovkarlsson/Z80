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
// loop - single-threaded, no locking/races in a codebase that had never
// needed any at the time this was first written (a real architectural
// choice made with the user - see the plan preserved at
// ~/.claude/plans/mellow-cooking-parrot.md for the full reasoning).
// Live SN76477 audio (audio_callback() below) is the one deliberate
// exception, added later, scoped via the same plan file reused for that
// sub-step: SDL2 runs its own real-time audio thread internally, handed
// the current sound register through a single atomic byte
// (AppState.live_sound_register) rather than a queue or lock - the
// narrowest concurrency surface that could actually work for real-time
// audio, not a reason to abandon the single-threaded design everywhere
// else.
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
#include <math.h>
#include <stdatomic.h>

#include <gtk/gtk.h>
#include <glib-unix.h>
#include <SDL2/SDL.h>

#include "../../../z80core/z80.h"
#include "../../../abc80/emu/src/step.h"
#include "../../../abc80/emu/src/disk.h"
#include "../../../abc80/emu/src/keyboard.h"
#include "../../../abc80/emu/src/video_timing.h"
#include "../../../abc80/emu/src/chargen.h"
#include "../../../abc80/emu/src/sound.h"
#include "../../../abc80/emu/src/cassette.h"
#include "../../../abc80/emu/src/charset.h"

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

// Real cursor-blink rate - see abc80/emu/src/main.c's own ABC80_BLINK_HZ
// comment for the MAME-sourced derivation (m_blink_timer at
// attotime::from_hz(XTAL(11'980'800)/2/6/64/312/16)). Duplicated here for
// the same reason ABC80_CLOCK_HZ above is - a single constant, not logic,
// and main.c has no header of its own to export it from.
#define ABC80_BLINK_HZ 3.125

// Matches abc80/emu/src/sound.c's own private WAV_SAMPLE_RATE exactly -
// duplicated here (a single number, not logic) since sound.c doesn't
// expose it, the same small-constant duplication already used for
// ABC80_CLOCK_HZ/ABC80_BLINK_HZ above. Real-time audio wants a small
// buffer for low latency without risking underruns on a real device;
// 1024 sample frames at 44100Hz is ~23ms, a common, safe default.
#define ABC80_GTK_AUDIO_SAMPLE_RATE 44100
#define ABC80_GTK_AUDIO_BUFFER_SAMPLES 1024

// Text/background/border are all independently user-choosable at
// runtime now (the Colors menu, see activate() below), so these are
// just the startup defaults - real ABC80's own white-on-black, not
// asserted as any particular hardware's exact phosphor chromaticity.
// `--amber` (kept as a launch-time convenience alongside the menu, not
// replaced by it) overrides just ABC80_GTK_DEFAULT_TEXT to this swatch -
// not the real ABC80's own monitor color, but the Luxor ABC800's
// (ABC80's direct successor) well-known amber CRT option, which the
// user liked; FFB000 is a commonly used amber-phosphor value matching
// most terminal emulators' own built-in "amber" themes, not sourced
// from an ABC800 hardware manual (no primary source for the exact CRT
// phosphor chromaticity was available).
#define ABC80_GTK_DEFAULT_TEXT ((GdkRGBA){1.0, 1.0, 1.0, 1.0})
#define ABC80_GTK_DEFAULT_BG ((GdkRGBA){0.0, 0.0, 0.0, 1.0})
#define ABC80_GTK_AMBER_TEXT ((GdkRGBA){0xFF / 255.0, 0xB0 / 255.0, 0x00 / 255.0, 1.0})

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

// User-requested: the drawing area was flush against the window edges on
// all sides, making the window look like the "monitor" was crammed into
// its own frame. A plain widget margin (not part of the emulated
// picture at all - applied to the GtkDrawingArea itself, outside
// draw_screen()'s own cairo_scale()'d coordinate space) puts real empty
// space around it, closer to how a real monitor sits inset within its
// own bezel.
#define ABC80_GTK_CANVAS_MARGIN 24

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
    Abc80SoundLog sound_log; // required by abc80_step()'s signature; drives live_sound_register below
    uint64_t total_cycles;
    uint64_t next_pio_interrupt_at;
    struct timespec run_start_time;
    double last_render_sec;
    bool cursor_blink_phase;
    bool ram32k_enabled;
    GdkRGBA text_color;
    GdkRGBA canvas_bg_color;
    GdkRGBA border_color;
    GtkCssProvider *canvas_css_provider; // backs border_color - see update_canvas_css()
    const char *quickload_path;
    bool quickload_done;
    const char *quicksave_path;
    GtkWidget *window;
    GtkWidget *drawing_area;
    guint timer_source_id;
    // Live audio (see audio_callback() below) - the only two fields in
    // this struct ever touched from a thread other than GTK's own main
    // one. live_sound_register is written once per timer tick (main
    // thread) and read every audio buffer fill (SDL's own real-time
    // audio thread) - a single atomic byte, deliberately not a queue or
    // lock, per this milestone's own scoping decision to keep this
    // app's first real concurrency surface as narrow as possible.
    // live_sound_phase is the opposite: owned exclusively by the audio
    // thread, never touched from the main thread at all.
    _Atomic uint8_t live_sound_register;
    double live_sound_phase;
    SDL_AudioDeviceID audio_device;
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

    cairo_set_source_rgb(cr, app->canvas_bg_color.red, app->canvas_bg_color.green, app->canvas_bg_color.blue);
    cairo_paint(cr);

    double sx = (double)width / ABC80_GTK_PIXEL_WIDTH;
    double sy = (double)height / ABC80_GTK_PIXEL_HEIGHT;
    cairo_scale(cr, sx, sy);
    cairo_set_source_rgb(cr, app->text_color.red, app->text_color.green, app->text_color.blue);

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

            if ((data & 0x80) != 0 && app->cursor_blink_phase) { // real ABC80_BLINK_HZ blink, matching render.c's own cursor && blink_phase gate
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
//
// Real Ctrl-<letter> chords (Ctrl-C to break a running program being the
// user-reported gap - real ABC80 keyboard input treats it as a plain
// 0x03 byte reaching the ROM's own break-combo check, per
// abc80/emu/src/main.c's own raw-terminal-mode comment; ABC80_BASIC_
// REFERENCE.md's Keyboard section documents Ctrl-X, "backspace the whole
// line," the same way) need the same translation `--interactive`'s raw
// terminal mode gets for free from the host tty driver - GDK, unlike a
// tty, reports the plain letter keyval plus a separate Control modifier
// bit rather than pre-folding it into a single control-code byte, so
// this maps it by hand: Ctrl-A through Ctrl-Z become bytes 0x01-0x1A,
// the standard ASCII control-code convention every real terminal already
// uses for this. Checked before the plain-key switch below so a Ctrl
// chord never falls through to being typed as its own bare letter.
static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval,
                                guint keycode, GdkModifierType state, gpointer user_data) {
    (void)controller;
    (void)keycode;
    (void)user_data;

    int ascii = -1;
    guint lower = gdk_keyval_to_lower(keyval);
    if ((state & GDK_CONTROL_MASK) && lower >= GDK_KEY_a && lower <= GDK_KEY_z) {
        ascii = (int)(lower - GDK_KEY_a + 1);
    } else {
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

// Runs on SDL's own real-time audio thread (started internally by
// SDL_OpenAudioDevice() - see main() below), never on GTK's main thread.
// Reads app->live_sound_register (the one value the main thread hands
// off, atomically - see on_timer_tick()'s own comment) and fills the
// buffer via abc80_sound_live_sample() (sound.h), which owns
// app->live_sound_phase entirely - the main thread never touches it.
// This function's own math (the incremental-phase square wave, gated by
// abc80_sound_is_steady_vco_tone()) is verified independently via
// bin/abc80-sound-demo --live, the same "prove it against known input"
// discipline abc80_sound_render_wav() already had - not just assumed
// correct because it compiles.
static void audio_callback(void *userdata, Uint8 *stream, int len) {
    AppState *app = userdata;
    int16_t *samples = (int16_t *)stream;
    int n = len / (int)sizeof(int16_t);
    uint8_t reg = atomic_load(&app->live_sound_register);
    for (int i = 0; i < n; i++) {
        samples[i] = abc80_sound_live_sample(reg, &app->live_sound_phase, ABC80_GTK_AUDIO_SAMPLE_RATE);
    }
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
        // --quickload injection point, identical to abc80/emu/src/main.c's
        // own: 0x02AA is the ROM's line-reading routine entry, the one
        // address confirmed (by that file's own extensive comment) to run
        // after BASIC's boot-time BOFA/EOFA reset loop has fully exited
        // but before any keyboard input can be read - the only safe place
        // to inject a saved program without a race against either.
        if (!app->quickload_done && app->cpu.pc == 0x02AA) {
            abc80_cassette_quickload(app->ram, app->quickload_path);
            app->quickload_done = true;
        }

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

    // Hands the current SN76477 register byte off to the audio thread -
    // once per tick (not per instruction; 5ms is far under any audio
    // buffering latency), after the batch loop above so this always
    // reflects the truly-current state. No changes needed to the shared
    // step.c/abc80_sound_write() write path itself - this just reads
    // what it already recorded. The 0x01 default (disabled) matches
    // abc80_sound_render_wav()'s own default for an empty log.
    atomic_store(&app->live_sound_register,
                 app->sound_log.count > 0 ? app->sound_log.events[app->sound_log.count - 1].data : 0x01);

    if (elapsed_real - app->last_render_sec >= ABC80_RENDER_INTERVAL_SEC) {
        app->last_render_sec = elapsed_real;
        // Same real ABC80_BLINK_HZ-derived phase computation as
        // abc80/emu/src/main.c's own --interactive loop.
        app->cursor_blink_phase = fmod(elapsed_real, 1.0 / ABC80_BLINK_HZ) < (0.5 / ABC80_BLINK_HZ);
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

    // Window close is this app's natural analogue of abc80/emu/src/main.c's
    // own end-of-run --quicksave point (that CLI has a bounded instruction
    // loop with a real exit; this window instead runs until closed).
    if (app->quicksave_path) {
        abc80_cassette_quicksave(app->ram, app->quicksave_path);
    }

    if (app->audio_device != 0) {
        SDL_CloseAudioDevice(app->audio_device);
        app->audio_device = 0;
    }
}

// SIGINT/SIGTERM (Ctrl-C in the launching terminal, or a plain `kill`)
// otherwise just terminates the process mid-instruction, silently
// dropping a pending --quicksave the same way unplugging a real machine
// would - a real gap, not just a testing convenience, so this is wired
// up unconditionally rather than behind a debug flag. g_unix_signal_add()
// (not a raw signal() handler) delivers the signal as a normal GLib main-
// loop callback, so it's safe to touch GTK/Cairo state from here, unlike
// a true async-signal-context handler. Destroying the window drives the
// same on_window_destroy() path a real close-button click would - timer
// cleanup and the --quicksave flush both happen exactly once, from a
// single real code path, rather than a second copy of that logic here.
static gboolean on_unix_signal(gpointer user_data) {
    GtkWindow *window = user_data;
    gtk_window_destroy(window);
    return G_SOURCE_REMOVE;
}

// Cmd-Q (via the app-level "quit" action and its <Primary>Q accelerator,
// registered in main() below - <Primary> is Cmd on macOS, Ctrl elsewhere).
// Destroys the real window rather than calling g_application_quit()
// directly, for the identical reason on_unix_signal() above does: it
// drives the same on_window_destroy() path a close-button click would,
// so timer cleanup and a pending --quicksave flush happen from the one
// real code path instead of a second copy of that logic here.
static void on_quit_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    (void)parameter;
    AppState *app = user_data;
    if (app->window) {
        gtk_window_destroy(GTK_WINDOW(app->window));
    }
}

// File menu actions (win.save-program/win.load-program/win.take-screenshot,
// see activate() below for the GMenu/GtkPopoverMenuBar wiring). These
// directly answer the real gap that prompted this menu in the first
// place: BASIC's own interactive SAVE/LOAD hang forever, since real
// cassette hardware isn't emulated (see abc80/docs/ABC80_ROADMAP.md's
// Milestone 4) - "Save Program"/"Load Program" instead call the same
// abc80_cassette_quicksave()/abc80_cassette_quickload() the CLI's own
// --quicksave/--quickload flags already use (cassette.h), just from a
// menu instead of a command-line flag chosen before launch. Uses the
// modern async GtkFileDialog (GTK 4.10+) rather than the older
// GtkFileChooserDialog, which is deprecated as of this GTK4 version.
//
// "Load Program" works the same way --quickload does: it overwrites
// BOFA..EOFA and fixes up EOFA/HEAD immediately, regardless of whatever
// BASIC happens to be doing at the moment (there's no "wait for a safe
// point" here the way the CLI's PC==0x02AA boot-time trigger provides,
// since a menu click can land at any arbitrary instant) - safe from a
// memory-corruption standpoint (menu actions and abc80_step() both run
// on the same GTK main thread, never concurrently), but the natural
// place to use it is a fresh/empty BASIC session (right after boot, or
// after typing NEW), the same real-hardware expectation loading a
// cassette program implies.
//
// User-requested: also support plain-text .bas, matching real ABC80
// BASIC's own "LIST filename" ("saves the program to cassette or floppy
// *uncompressed*, as text" - ABC80_BASIC_REFERENCE.md), as distinct from
// SAVE's tokenized/compressed .bac. The dialogs below offer both as
// GtkFileFilter choices; the chosen path's extension picks the format,
// defaulting to .bac for anything else (preserving prior behavior for a
// bare filename).
//
// Unlike .bac (a byte-for-byte copy of BASIC's own tokenized [BOFA,
// EOFA) representation, no token decoding needed), .bas needs real
// BASIC source text - the tokens are opaque bytes, not ASCII. Rather
// than reverse-engineer the full token table, this drives the real
// ROM's own LIST command (which already does correct detokenization)
// through the exact same keyboard-injection mechanism poll_stdin_byte()/
// on_key_pressed() already use, and reads the result back off video RAM
// - the same "let the real ROM do the work at the I/O boundary"
// principle behind the disk bypass and BDOS/BIOS interception
// elsewhere in this project, not a new pattern invented for this.
//
// Two real facts below are grounded via direct execution
// (bin/abc80 --interactive, scripted input), not assumed:
//   - `LIST n-n` (a range with identical start/end) lists exactly one
//     line - the key fact that makes this scroll-proof: listing one
//     line at a time never risks anything scrolling off a 24-row
//     screen, regardless of how long the real program is.
//   - `PRINT CHR$(12)` clears the screen and resets the cursor to a
//     fixed, repeatable position, so `PRINT CHR$(12)\rLIST n-n\r`
//     always lands the command's echo on video RAM row 3 and the
//     listed line's own text on row 4 (0-indexed), verified against
//     three different line numbers in a row, not just the first.
#define ABC80_GTK_BAS_MAX_LINES 2000
#define ABC80_GTK_BAS_ECHO_ROW 3
#define ABC80_GTK_BAS_CONTENT_ROW 4

// Walks BASIC's own tokenized program storage directly - [BOFA, EOFA),
// see cassette.h - extracting just the line numbers, in order, without
// decoding a single token. Grounded against a real quicksave of a known
// 4-line program (dumped and hand-verified byte-for-byte against this
// exact walk): each stored line is [length byte][line number, 2 bytes
// little-endian][...tokens...][0x0D], where the length byte is the
// whole line's own size *including itself* - confirmed by checking that
// offset+length always lands exactly on the next line's own length
// byte (or, for the last line, one byte before the final terminator
// byte cassette.c's own comment already documents). A zero length byte
// stops the walk immediately rather than looping forever - defensive,
// not expected to fire on any real program.
static int extract_line_numbers(const uint8_t *ram, uint16_t *out, int max_lines) {
    uint16_t bofa = (uint16_t)(ram[ABC80_BOFA_ADDR] | (ram[(uint16_t)(ABC80_BOFA_ADDR + 1)] << 8));
    uint16_t eofa = (uint16_t)(ram[ABC80_EOFA_ADDR] | (ram[(uint16_t)(ABC80_EOFA_ADDR + 1)] << 8));
    int count = 0;
    uint16_t offset = bofa;
    while (offset < eofa && count < max_lines) {
        uint8_t length = ram[offset];
        if (length == 0) break;
        out[count++] = (uint16_t)(ram[(uint16_t)(offset + 1)] | (ram[(uint16_t)(offset + 2)] << 8));
        offset = (uint16_t)(offset + length);
    }
    return count;
}

// Advances emulation directly (not through on_timer_tick()'s real-time
// pacing - this is a one-shot, bounded batch driven synchronously from
// a menu action, the same style --quickload's own PC==0x02AA trigger
// already works within) for `count` instructions, or until a halt.
static void pump_steps(AppState *app, int count) {
    for (int i = 0; i < count; i++) {
        int cycles = abc80_step(&app->cpu, app->ram, &app->sound_log,
                                 &app->total_cycles, &app->next_pio_interrupt_at);
        if (cycles < 0) return; // halted - nothing more this helper can do
    }
}

// Types one line (any direct-mode command, or a program line) plus a
// trailing CR through the real keyboard-input path - the same
// abc80_keyboard_press()/abc80_keyboard_ready_for_next() pair
// on_key_pressed() and the stdin-scripting path already use, just
// pumped synchronously instead of paced by real time. The pump counts
// below are generous fixed budgets (not tuned to a minimum) - confirmed
// sufficient by direct testing (a too-small budget shows up immediately
// as truncated/blank captures), matching how quickload's own boot-delay
// constant was chosen generously rather than to a tight minimum.
static void inject_line(AppState *app, const char *text) {
    for (const char *p = text; *p; p++) {
        while (!abc80_keyboard_ready_for_next()) {
            pump_steps(app, 200);
        }
        abc80_keyboard_press((uint8_t)*p);
        pump_steps(app, 2000);
    }
    while (!abc80_keyboard_ready_for_next()) {
        pump_steps(app, 200);
    }
    abc80_keyboard_press(0x0D);
    pump_steps(app, 30000);
}

// Reverse of abc80_charset_codepoint() (charset.h) for the nine real
// Swedish-alphabet substitutions ABC80's TEXT-mode character set makes
// over plain ASCII - needed so a .bas file containing e.g. Å/Ä/Ö
// (whether round-tripped from this app's own Save, or typed by hand in
// a text editor) re-types as the correct single ABC80 character byte on
// Load, not two separate wrong ones. Anything else non-ASCII falls back
// to '?' rather than silently corrupting the re-tokenization.
static uint8_t unicode_codepoint_to_abc80_char(uint32_t cp) {
    switch (cp) {
        case 0xC9: return 0x40; case 0xC4: return 0x5B; case 0xD6: return 0x5C;
        case 0xC5: return 0x5D; case 0xDC: return 0x5E; case 0xE9: return 0x60;
        case 0xE4: return 0x7B; case 0xF6: return 0x7C; case 0xE5: return 0x7D;
        case 0xFC: return 0x7E;
        default:
            return (cp < 0x80) ? (uint8_t)cp : (uint8_t)'?';
    }
}

// Minimal UTF-8 decoder for exactly the range this file ever needs to
// read back (plain ASCII, plus 2-byte sequences for the nine Swedish
// letters above) - not general-purpose. Malformed or unsupported
// (3+-byte) sequences fall back to one '?' per lead byte rather than
// misparsing the rest of the line. Returned string is g_malloc()'d
// (length <= `len`, since every decoded character is exactly one output
// byte); caller g_free()s it.
static char *decode_utf8_to_abc80(const char *utf8, size_t len) {
    char *out = g_malloc(len + 1);
    size_t out_len = 0, i = 0;
    while (i < len) {
        unsigned char b0 = (unsigned char)utf8[i];
        uint32_t cp;
        size_t adv;
        if (b0 < 0x80) {
            cp = b0;
            adv = 1;
        } else if ((b0 & 0xE0) == 0xC0 && i + 1 < len) {
            unsigned char b1 = (unsigned char)utf8[i + 1];
            cp = (uint32_t)((b0 & 0x1F) << 6) | (b1 & 0x3F);
            adv = 2;
        } else {
            cp = '?';
            adv = 1;
        }
        out[out_len++] = (char)unicode_codepoint_to_abc80_char(cp);
        i += adv;
    }
    out[out_len] = '\0';
    return out;
}

// Encodes one ABC80 TEXT-mode character (via the same, already-verified
// abc80_charset_codepoint() render.c itself uses) as UTF-8 into `s` -
// only ever 1 or 2 bytes here, since every codepoint that table
// produces is <=0xFC.
static void append_abc80_char_utf8(GString *s, uint8_t abc80_char) {
    uint32_t cp = abc80_charset_codepoint(abc80_char);
    if (cp < 0x80) {
        g_string_append_c(s, (char)cp);
    } else {
        g_string_append_c(s, (char)(0xC0 | (cp >> 6)));
        g_string_append_c(s, (char)(0x80 | (cp & 0x3F)));
    }
}

// Reads one video RAM row back as real UTF-8 text via the identical
// TEXT-mode decode abc80_render_frame() (render.c) already uses -
// row/col addressed through abc80_videoram_addr() (video_timing.h), the
// same as draw_screen() itself - and right-trims trailing spaces.
// Returned string is g_malloc()'d (via GString); caller g_free()s it.
static char *capture_row_text(AppState *app, int row) {
    GString *s = g_string_new(NULL);
    const uint8_t *videoram = &app->ram[0x7C00];
    for (int col = 0; col < ABC80_GTK_COLS; col++) {
        uint16_t addr = abc80_videoram_addr((uint8_t)row, (uint8_t)col);
        uint8_t character = videoram[addr] & 0x7F;
        append_abc80_char_utf8(s, character);
    }
    while (s->len > 0 && s->str[s->len - 1] == ' ') {
        g_string_truncate(s, s->len - 1);
    }
    return g_string_free(s, FALSE);
}

// Orchestrates Save-as-.bas: walk the program's real line numbers, list
// each one individually against a freshly-cleared screen, capture and
// accumulate the real ROM-detokenized text. Known, stated limitation
// (not silently mishandled): a source line wider than one screen row
// (40 columns) would wrap onto a second video row this doesn't capture.
// Returned string is g_malloc()'d; caller g_free()s it.
static char *build_bas_text(AppState *app) {
    static uint16_t line_numbers[ABC80_GTK_BAS_MAX_LINES];
    int count = extract_line_numbers(app->ram, line_numbers, ABC80_GTK_BAS_MAX_LINES);
    GString *out = g_string_new(NULL);
    for (int i = 0; i < count; i++) {
        char cmd[32];
        inject_line(app, "PRINT CHR$(12)");
        snprintf(cmd, sizeof(cmd), "LIST %u-%u", line_numbers[i], line_numbers[i]);
        inject_line(app, cmd);
        char *row_text = capture_row_text(app, ABC80_GTK_BAS_CONTENT_ROW);
        g_string_append(out, row_text);
        g_string_append_c(out, '\n');
        g_free(row_text);
    }
    (void)ABC80_GTK_BAS_ECHO_ROW; // documents the layout; not itself read
    return g_string_free(out, FALSE);
}

// Orchestrates Load-of-.bas: re-types each line (decoded back to real
// ABC80 character bytes) through the real keyboard path, the same way a
// human retyping a printed listing on real hardware would - there's no
// ROM routine that ingests raw text as a data blob, so this *is* the
// real mechanism, not a shortcut. Real caveat, distinct from .bac Load's
// full-replace quickload: typing a line number that already exists
// *replaces* that line (real BASIC behavior), so this merges into
// rather than replaces a non-empty program - best used on a fresh
// session, the same real-hardware expectation .bac Load already
// documents above.
static void load_bas_text(AppState *app, const char *text) {
    const char *line_start = text;
    while (*line_start) {
        const char *line_end = strchr(line_start, '\n');
        size_t len = line_end ? (size_t)(line_end - line_start) : strlen(line_start);
        if (len > 0 && line_start[len - 1] == '\r') len--; // tolerate CRLF files
        if (len > 0) {
            char *decoded = decode_utf8_to_abc80(line_start, len);
            inject_line(app, decoded);
            g_free(decoded);
        }
        if (!line_end) break;
        line_start = line_end + 1;
    }
}

static gboolean path_has_suffix_ci(const char *path, const char *suffix) {
    size_t path_len = strlen(path);
    size_t suf_len = strlen(suffix);
    return path_len >= suf_len && g_ascii_strcasecmp(path + path_len - suf_len, suffix) == 0;
}

// Shared by on_save_program()/on_load_program() below - the chosen
// path's own extension (checked in the response callbacks via
// path_has_suffix_ci()) is what actually picks the format; these
// filters just drive the file picker's own display/typed-extension
// behavior. Default filter stays .bac, preserving prior behavior for a
// bare filename with no extension typed.
static void set_program_file_filters(GtkFileDialog *dialog) {
    GtkFileFilter *bac_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(bac_filter, "ABC80 Program (*.bac)");
    gtk_file_filter_add_suffix(bac_filter, "bac");

    GtkFileFilter *bas_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(bas_filter, "ABC80 BASIC Text (*.bas)");
    gtk_file_filter_add_suffix(bas_filter, "bas");

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, bac_filter);
    g_list_store_append(filters, bas_filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, bac_filter);
    g_object_unref(bac_filter);
    g_object_unref(bas_filter);
    g_object_unref(filters);
}

static void on_save_program_response(GObject *source, GAsyncResult *res, gpointer user_data) {
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    AppState *app = user_data;
    GFile *file = gtk_file_dialog_save_finish(dialog, res, NULL);
    if (!file) return;
    char *path = g_file_get_path(file);
    if (path) {
        if (path_has_suffix_ci(path, ".bas")) {
            char *text = build_bas_text(app);
            g_file_set_contents(path, text, -1, NULL);
            g_free(text);
        } else {
            abc80_cassette_quicksave(app->ram, path);
        }
        g_free(path);
    }
    g_object_unref(file);
}

static void on_save_program(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    (void)parameter;
    AppState *app = user_data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Save Program");
    gtk_file_dialog_set_initial_name(dialog, "program");
    set_program_file_filters(dialog);
    gtk_file_dialog_save(dialog, GTK_WINDOW(app->window), NULL, on_save_program_response, app);
}

static void on_load_program_response(GObject *source, GAsyncResult *res, gpointer user_data) {
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    AppState *app = user_data;
    GFile *file = gtk_file_dialog_open_finish(dialog, res, NULL);
    if (!file) return;
    char *path = g_file_get_path(file);
    if (path) {
        if (path_has_suffix_ci(path, ".bas")) {
            char *text = NULL;
            if (g_file_get_contents(path, &text, NULL, NULL)) {
                load_bas_text(app, text);
                g_free(text);
            }
        } else {
            abc80_cassette_quickload(app->ram, path);
        }
        g_free(path);
    }
    g_object_unref(file);
}

static void on_load_program(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    (void)parameter;
    AppState *app = user_data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Load Program");
    set_program_file_filters(dialog);
    gtk_file_dialog_open(dialog, GTK_WINDOW(app->window), NULL, on_load_program_response, app);
}

// Renders through the exact same draw_screen() the live window uses -
// against an offscreen surface instead of the real widget, but sharing
// one code path guarantees the saved PNG always matches what's on
// screen (amber palette, cursor blink phase, GRAPHICS-mode pixels and
// all) rather than a second, potentially-drifting reimplementation.
// draw_screen()'s own `area` parameter is unused in its body (cast to
// void immediately), so passing NULL here is safe.
static void on_screenshot_response(GObject *source, GAsyncResult *res, gpointer user_data) {
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    AppState *app = user_data;
    GFile *file = gtk_file_dialog_save_finish(dialog, res, NULL);
    if (!file) return;
    char *path = g_file_get_path(file);
    if (path) {
        int w = ABC80_GTK_PIXEL_WIDTH * ABC80_GTK_SCALE;
        int h = ABC80_GTK_PIXEL_HEIGHT * ABC80_GTK_SCALE;
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t *cr = cairo_create(surface);
        draw_screen(NULL, cr, w, h, app);
        cairo_surface_write_to_png(surface, path);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
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
    gtk_file_dialog_set_initial_name(dialog, "abc80-screenshot.png");
    gtk_file_dialog_save(dialog, GTK_WINDOW(app->window), NULL, on_screenshot_response, app);
}

// Colors menu (win.text-color/win.background-color/win.border-color,
// see activate() below for the GMenu wiring) - user-requested: pick any
// text/canvas-background/border color at runtime via GTK's own built-in
// GtkColorDialog (the same async-dialog family as GtkFileDialog above,
// GTK 4.10+), rather than being limited to the fixed white/amber choice
// --amber offered. That flag still works as a launch-time convenience
// (see main()'s own argument parsing), but now just seeds text_color's
// initial value - all three colors are freely repickable afterward.

// Regenerates layout_box's own CSS rule from the current border_color -
// called once at startup and again every time the Border Color dialog
// picks a new value. A GtkCssProvider's content can be reloaded in
// place (gtk_css_provider_load_from_string() on the same, already-
// registered provider), so this doesn't need to re-register a new
// provider each time border_color changes.
static void update_canvas_css(AppState *app) {
    char css[160];
    snprintf(css, sizeof(css), ".abc80-canvas-area { background-color: rgb(%d,%d,%d); }",
             (int)(app->border_color.red * 255 + 0.5),
             (int)(app->border_color.green * 255 + 0.5),
             (int)(app->border_color.blue * 255 + 0.5));
    gtk_css_provider_load_from_string(app->canvas_css_provider, css);
}

static void on_text_color_response(GObject *source, GAsyncResult *res, gpointer user_data) {
    GtkColorDialog *dialog = GTK_COLOR_DIALOG(source);
    AppState *app = user_data;
    GdkRGBA *rgba = gtk_color_dialog_choose_rgba_finish(dialog, res, NULL);
    if (!rgba) return;
    app->text_color = *rgba;
    gdk_rgba_free(rgba);
    if (app->drawing_area) gtk_widget_queue_draw(app->drawing_area);
}

static void on_text_color(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    (void)parameter;
    AppState *app = user_data;
    GtkColorDialog *dialog = gtk_color_dialog_new();
    gtk_color_dialog_set_title(dialog, "Text Color");
    gtk_color_dialog_choose_rgba(dialog, GTK_WINDOW(app->window), &app->text_color, NULL, on_text_color_response, app);
}

static void on_background_color_response(GObject *source, GAsyncResult *res, gpointer user_data) {
    GtkColorDialog *dialog = GTK_COLOR_DIALOG(source);
    AppState *app = user_data;
    GdkRGBA *rgba = gtk_color_dialog_choose_rgba_finish(dialog, res, NULL);
    if (!rgba) return;
    app->canvas_bg_color = *rgba;
    gdk_rgba_free(rgba);
    if (app->drawing_area) gtk_widget_queue_draw(app->drawing_area);
}

static void on_background_color(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    (void)parameter;
    AppState *app = user_data;
    GtkColorDialog *dialog = gtk_color_dialog_new();
    gtk_color_dialog_set_title(dialog, "Canvas Background Color");
    gtk_color_dialog_choose_rgba(dialog, GTK_WINDOW(app->window), &app->canvas_bg_color, NULL, on_background_color_response, app);
}

static void on_border_color_response(GObject *source, GAsyncResult *res, gpointer user_data) {
    GtkColorDialog *dialog = GTK_COLOR_DIALOG(source);
    AppState *app = user_data;
    GdkRGBA *rgba = gtk_color_dialog_choose_rgba_finish(dialog, res, NULL);
    if (!rgba) return;
    app->border_color = *rgba;
    gdk_rgba_free(rgba);
    update_canvas_css(app);
}

static void on_border_color(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    (void)parameter;
    AppState *app = user_data;
    GtkColorDialog *dialog = gtk_color_dialog_new();
    gtk_color_dialog_set_title(dialog, "Border Color");
    gtk_color_dialog_choose_rgba(dialog, GTK_WINDOW(app->window), &app->border_color, NULL, on_border_color_response, app);
}

// Save Preferences (win.save-preferences, Colors menu) - user-requested:
// persist the three Colors-menu settings so they don't reset to the
// defaults (or whatever --amber seeded) on the next launch. A GKeyFile
// at $XDG_CONFIG_HOME/abc80-gtk/prefs.ini (g_get_user_config_dir()'s own
// cross-platform resolution, not a hand-rolled macOS-specific path) -
// GdkRGBA's own gdk_rgba_to_string()/gdk_rgba_parse() round-trip cleanly
// through a single string value per color, so no custom serialization
// format is needed. There's no separate "Load Preferences" menu item -
// load_preferences() (called once from main(), before --amber can
// override it) runs automatically at every launch, which is the whole
// point of something being a *preference* rather than a one-off
// --quickload-style action the user repeats by hand each time.
static char *prefs_file_path(void) {
    return g_build_filename(g_get_user_config_dir(), "abc80-gtk", "prefs.ini", NULL);
}

static void load_preferences(AppState *app) {
    char *path = prefs_file_path();
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        char *text_str = g_key_file_get_string(kf, "Colors", "TextColor", NULL);
        char *bg_str = g_key_file_get_string(kf, "Colors", "BackgroundColor", NULL);
        char *border_str = g_key_file_get_string(kf, "Colors", "BorderColor", NULL);
        GdkRGBA rgba;
        if (text_str && gdk_rgba_parse(&rgba, text_str)) app->text_color = rgba;
        if (bg_str && gdk_rgba_parse(&rgba, bg_str)) app->canvas_bg_color = rgba;
        if (border_str && gdk_rgba_parse(&rgba, border_str)) app->border_color = rgba;
        g_free(text_str);
        g_free(bg_str);
        g_free(border_str);
    }
    g_key_file_free(kf);
    g_free(path);
}

static void on_save_preferences(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    (void)parameter;
    AppState *app = user_data;

    char *dir = g_build_filename(g_get_user_config_dir(), "abc80-gtk", NULL);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    GKeyFile *kf = g_key_file_new();
    char *text_str = gdk_rgba_to_string(&app->text_color);
    char *bg_str = gdk_rgba_to_string(&app->canvas_bg_color);
    char *border_str = gdk_rgba_to_string(&app->border_color);
    g_key_file_set_string(kf, "Colors", "TextColor", text_str);
    g_key_file_set_string(kf, "Colors", "BackgroundColor", bg_str);
    g_key_file_set_string(kf, "Colors", "BorderColor", border_str);
    g_free(text_str);
    g_free(bg_str);
    g_free(border_str);

    char *path = prefs_file_path();
    GError *error = NULL;
    if (g_key_file_save_to_file(kf, path, &error)) {
        printf("Saved preferences to '%s'\n", path);
    } else {
        fprintf(stderr, "Failed to save preferences to '%s': %s\n", path, error->message);
        g_error_free(error);
    }
    g_free(path);
    g_key_file_free(kf);
}

static void activate(GtkApplication *gtk_app, gpointer user_data) {
    AppState *app = user_data;

    GtkWidget *window = gtk_application_window_new(gtk_app);
    app->window = window;
    gtk_window_set_title(GTK_WINDOW(window), "ABC80");
    gtk_window_set_default_size(GTK_WINDOW(window), ABC80_GTK_PIXEL_WIDTH * ABC80_GTK_SCALE + 2 * ABC80_GTK_CANVAS_MARGIN,
                                 ABC80_GTK_PIXEL_HEIGHT * ABC80_GTK_SCALE + 2 * ABC80_GTK_CANVAS_MARGIN);

    // File menu (Save/Load Program, Take Screenshot) - a real,
    // in-window GtkPopoverMenuBar rather than relying on
    // gtk_application_set_menubar()'s desktop-shell-provided native
    // integration, which is inconsistent across platforms (works well
    // via macOS's global menu bar, but needs shell-specific settings on
    // X11/Wayland that aren't guaranteed to be on) - this way the menu
    // is always visible the same way regardless of platform/desktop.
    static const GActionEntry win_actions[] = {
        {"save-program", on_save_program, NULL, NULL, NULL, {0}},
        {"load-program", on_load_program, NULL, NULL, NULL, {0}},
        {"take-screenshot", on_take_screenshot, NULL, NULL, NULL, {0}},
        {"text-color", on_text_color, NULL, NULL, NULL, {0}},
        {"background-color", on_background_color, NULL, NULL, NULL, {0}},
        {"border-color", on_border_color, NULL, NULL, NULL, {0}},
        {"save-preferences", on_save_preferences, NULL, NULL, NULL, {0}},
    };
    g_action_map_add_action_entries(G_ACTION_MAP(window), win_actions, G_N_ELEMENTS(win_actions), app);

    // "<Primary>" is GTK's own portable primary-accelerator modifier -
    // Ctrl on Linux/Windows, Cmd on macOS - so this one call is the real,
    // idiomatic Cmd-S/Cmd-O on macOS the user asked for, not a hand-
    // rolled GDK_META_MASK check (which would only be correct on one
    // platform and would fight GTK's own accelerator dispatch, which
    // already runs before on_key_pressed() ever sees the event).
    gtk_application_set_accels_for_action(gtk_app, "win.save-program", (const char *[]){"<Primary>S", NULL});
    gtk_application_set_accels_for_action(gtk_app, "win.load-program", (const char *[]){"<Primary>O", NULL});

    GMenu *file_menu = g_menu_new();
    g_menu_append(file_menu, "Save Program…", "win.save-program");
    g_menu_append(file_menu, "Load Program…", "win.load-program");
    g_menu_append(file_menu, "Take Screenshot…", "win.take-screenshot");
    GMenu *colors_menu = g_menu_new();
    g_menu_append(colors_menu, "Text Color…", "win.text-color");
    g_menu_append(colors_menu, "Canvas Background Color…", "win.background-color");
    g_menu_append(colors_menu, "Border Color…", "win.border-color");
    GMenu *prefs_section = g_menu_new();
    g_menu_append(prefs_section, "Save Preferences", "win.save-preferences");
    g_menu_append_section(colors_menu, NULL, G_MENU_MODEL(prefs_section));
    g_object_unref(prefs_section);
    GMenu *menubar_model = g_menu_new();
    g_menu_append_submenu(menubar_model, "File", G_MENU_MODEL(file_menu));
    g_menu_append_submenu(menubar_model, "Colors", G_MENU_MODEL(colors_menu));
    GtkWidget *menu_bar = gtk_popover_menu_bar_new_from_model(G_MENU_MODEL(menubar_model));
    g_object_unref(file_menu);
    g_object_unref(colors_menu);
    g_object_unref(menubar_model);

    app->drawing_area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(app->drawing_area),
                                        ABC80_GTK_PIXEL_WIDTH * ABC80_GTK_SCALE);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(app->drawing_area),
                                         ABC80_GTK_PIXEL_HEIGHT * ABC80_GTK_SCALE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->drawing_area), draw_screen, app, NULL);
    // Real empty space around the canvas (not part of the emulated
    // picture - see ABC80_GTK_CANVAS_MARGIN's own comment) so the
    // "monitor" doesn't look crammed into the window frame.
    gtk_widget_set_margin_start(app->drawing_area, ABC80_GTK_CANVAS_MARGIN);
    gtk_widget_set_margin_end(app->drawing_area, ABC80_GTK_CANVAS_MARGIN);
    gtk_widget_set_margin_top(app->drawing_area, ABC80_GTK_CANVAS_MARGIN);
    gtk_widget_set_margin_bottom(app->drawing_area, ABC80_GTK_CANVAS_MARGIN);
    gtk_widget_set_halign(app->drawing_area, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(app->drawing_area, GTK_ALIGN_CENTER);

    // User-requested: the margin around the canvas (above) showed
    // through as the default GTK theme background - a different color
    // from the canvas itself, so it read as a visible border rather than
    // blending in. Painting a background behind drawing_area makes the
    // margin blend into (or, via the Colors menu, deliberately contrast
    // with) the canvas, since drawing_area's own opaque paint covers its
    // own allocated area regardless - only the margin (unclaimed by
    // drawing_area) ever shows this background at all.
    //
    // A real bug this had at first, user-reported: applying this CSS
    // class to the *outer* box (containing both menu_bar and
    // drawing_area) also painted it in behind the menu bar wherever the
    // menu bar's own theme doesn't give it a fully opaque background of
    // its own - GtkPopoverMenuBar's default styling in at least one
    // theme doesn't - producing illegibly-low-contrast (dark-on-black)
    // menu text, since the menu's text color is themed for a normal
    // light/gray menu background, not whatever the user just picked for
    // the ABC80 canvas. Fixed by scoping the CSS class to a dedicated
    // canvas_wrapper box holding *only* drawing_area, as menu_bar's own
    // sibling rather than its cousin - border_color now can't reach
    // anywhere near the menu bar's own rendering, regardless of what
    // color it's set to.
    GtkWidget *canvas_wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(canvas_wrapper), app->drawing_area);

    // Kept on the AppState (not unref'd here) since Border Color's own
    // dialog needs to reload this exact provider's content later -
    // gtk_css_provider_load_from_string() on an already-registered
    // provider updates it in place, no need to re-register a new one
    // each time the color changes.
    app->canvas_css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                                GTK_STYLE_PROVIDER(app->canvas_css_provider),
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    update_canvas_css(app);
    gtk_widget_add_css_class(canvas_wrapper, "abc80-canvas-area");

    GtkWidget *layout_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(layout_box), menu_bar);
    gtk_box_append(GTK_BOX(layout_box), canvas_wrapper);
    gtk_window_set_child(GTK_WINDOW(window), layout_box);

    GtkEventController *key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), app);
    gtk_widget_add_controller(window, key_controller);

    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), app);
    g_unix_signal_add(SIGINT, on_unix_signal, window);
    g_unix_signal_add(SIGTERM, on_unix_signal, window);

    clock_gettime(CLOCK_MONOTONIC, &app->run_start_time);
    app->last_render_sec = 0.0;
    app->timer_source_id = g_timeout_add(ABC80_GTK_TIMER_INTERVAL_MS, on_timer_tick, app);

    gtk_window_present(GTK_WINDOW(window));
}

// Mirrors abc80/emu/src/main.c's own print_usage() style (one line per
// flag plus an indented description) - that file's the CLI's own
// dedicated -h/--help handler; this one had none at all until a user
// noticed --amber (added later than --disk/--ram32k) was missing from
// the terse one-line fallback message an unrecognized argument produced
// (the *actual* usage text, past tense - see the "Unknown argument"
// branch below - already listed every flag correctly by the time this
// was reported, since it's generated from the same source; the real gap
// was not having a proper --help at all, unlike the CLI).
static void print_usage(const char *prog) {
    printf("Usage:\n");
    printf("  %s [rom_dir] [--disk FILE] [--ram32k] [--amber] [--quickload FILE] [--quicksave FILE]\n", prog);
    printf("\n");
    printf("  rom_dir            Directory containing the four ROM images\n");
    printf("                     (default: resources/rom - run from inside abc80/)\n");
    printf("  --disk FILE        Load the real ABC-DOS ROM at 0x6000 and back its floppy\n");
    printf("                     I/O with a host file (created if it doesn't exist) -\n");
    printf("                     see abc80/docs/ABC80_ROADMAP.md for how this bypass works.\n");
    printf("  --ram32k           Model the real 16KB RAM-expansion mod at 0x8000-0xBFFF\n");
    printf("                     (default: base 16K machine - that range floats)\n");
    printf("  --amber            Start with the Luxor ABC800's amber-on-black look (not\n");
    printf("                     the base ABC80's own) instead of the default white-on-black -\n");
    printf("                     text/background/border colors can all be changed afterward\n");
    printf("                     from the Colors menu regardless of this flag\n");
    printf("  --quickload FILE   Inject a saved program into BASIC's program storage\n");
    printf("                     area once boot init has run (see cassette.h) - also\n");
    printf("                     available from the File menu as \"Load Program...\"\n");
    printf("  --quicksave FILE   Dump BASIC's current program storage (BOFA..EOFA) to\n");
    printf("                     FILE when the window closes - also available from the\n");
    printf("                     File menu as \"Save Program...\"\n");
}

int main(int argc, char *argv[]) {
    const char *rom_dir = "resources/rom";
    const char *disk_path = NULL;
    const char *quickload_path = NULL;
    const char *quicksave_path = NULL;
    int arg_i = 1;
    if (arg_i < argc && argv[arg_i][0] != '-') {
        rom_dir = argv[arg_i++];
    }
    for (; arg_i < argc; arg_i++) {
        if (strcmp(argv[arg_i], "-h") == 0 || strcmp(argv[arg_i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[arg_i], "--disk") == 0 && arg_i + 1 < argc) {
            disk_path = argv[++arg_i];
        } else if (strcmp(argv[arg_i], "--quickload") == 0 && arg_i + 1 < argc) {
            quickload_path = argv[++arg_i];
        } else if (strcmp(argv[arg_i], "--quicksave") == 0 && arg_i + 1 < argc) {
            quicksave_path = argv[++arg_i];
        } else if (strcmp(argv[arg_i], "--ram32k") == 0 || strcmp(argv[arg_i], "--amber") == 0) {
            // handled below, after AppState exists
        } else {
            fprintf(stderr, "Unknown argument: %s\n\n", argv[arg_i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    static AppState app;
    memset(&app, 0, sizeof(app));
    g_app_for_bus_hook = &app;
    app.quickload_path = quickload_path;
    app.quickload_done = (quickload_path == NULL);
    app.quicksave_path = quicksave_path;
    app.text_color = ABC80_GTK_DEFAULT_TEXT;
    app.canvas_bg_color = ABC80_GTK_DEFAULT_BG;
    app.border_color = ABC80_GTK_DEFAULT_BG; // matches canvas by default - see update_canvas_css()
    load_preferences(&app); // overrides the three colors above if a saved prefs.ini exists

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ram32k") == 0) app.ram32k_enabled = true;
        // --amber wins over a saved preference too, as an explicit choice for this one launch.
        if (strcmp(argv[i], "--amber") == 0) app.text_color = ABC80_GTK_AMBER_TEXT;
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

    // Live audio (see audio_callback() above). SDL_INIT_AUDIO only -
    // never SDL_INIT_VIDEO/a window - so there's no overlap with GTK's
    // own Cocoa/X11/Wayland windowing, just SDL's own real-time audio
    // thread running independently underneath GTK's main loop.
    // allowed_changes=0 requests the exact format (mono/16-bit/44100Hz,
    // matching sound.c's own WAV format) rather than risking SDL's own
    // sample-rate/format conversion path.
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "Warning: SDL_Init(SDL_INIT_AUDIO) failed: %s - continuing without audio\n", SDL_GetError());
    } else {
        SDL_AudioSpec want;
        SDL_zero(want);
        want.freq = ABC80_GTK_AUDIO_SAMPLE_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = ABC80_GTK_AUDIO_BUFFER_SAMPLES;
        want.callback = audio_callback;
        want.userdata = &app;
        app.audio_device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (app.audio_device == 0) {
            fprintf(stderr, "Warning: SDL_OpenAudioDevice failed: %s - continuing without audio\n", SDL_GetError());
        } else {
            SDL_PauseAudioDevice(app.audio_device, 0);
        }
    }

    GtkApplication *gtk_app = gtk_application_new(NULL, G_APPLICATION_DEFAULT_FLAGS);

    // App-level quit action + its Cmd-Q (macOS)/Ctrl-Q (elsewhere)
    // accelerator - registered on the GApplication itself (not the
    // window's own "win" action group) since quitting the whole app,
    // unlike Save/Load Program, isn't really a per-window concept.
    // Safe to register before the window exists: on_quit_action() reads
    // app.window through the same static AppState activate() fills in,
    // and no accelerator can actually fire before activate() has already
    // run (there's no window yet to have focus).
    GSimpleAction *quit_action = g_simple_action_new("quit", NULL);
    g_signal_connect(quit_action, "activate", G_CALLBACK(on_quit_action), &app);
    g_action_map_add_action(G_ACTION_MAP(gtk_app), G_ACTION(quit_action));
    g_object_unref(quit_action);
    gtk_application_set_accels_for_action(gtk_app, "app.quit", (const char *[]){"<Primary>Q", NULL});

    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), &app);
    int status = g_application_run(G_APPLICATION(gtk_app), 0, NULL);
    g_object_unref(gtk_app);
    return status;
}
