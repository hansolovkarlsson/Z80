#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "cpu.h"
#include "mmu.h"
#include "cart.h"
#include "ppu.h"
#include "timer.h"
#include "joypad.h"
#include "apu.h"

// Phase 5 bring-up driver: load a real cartridge, run it, ticking the
// PPU/timer/APU alongside the CPU each step (same clock, same call
// site - see gb_ppu_step()'s own comment), and either print serial
// output (Blargg-style text tests), dump a rendered frame as a PPM
// image (--ppm), or dump generated audio as a WAV file (--wav). No
// real interactive input source exists yet (a real front end reading a
// host keyboard/controller is Phase 7's job) - by default the joypad
// reports "nothing pressed" for the whole run. --input (added Phase 6)
// is a scripted substitute for that, just enough to actually play a
// real game deterministically for validation purposes - see
// parse_input_script()/apply_input_events() below.

#define WAV_SAMPLE_RATE 44100
#define MAX_INPUT_EVENTS 256

static void serial_putc(uint8_t byte) {
    putchar(byte);
    fflush(stdout);
}

// DMG shade index (0=white..3=black) -> 8-bit grayscale sample, evenly
// spaced across the full 0-255 range - matches how the dmg-acid2
// reference PNG (a 2-bit grayscale image) maps its own 4 shades, so a
// byte-for-byte comparison against it doesn't need any special-casing.
static uint8_t shade_to_gray(uint8_t shade) {
    switch (shade) {
        case 0: return 255;
        case 1: return 170;
        case 2: return 85;
        default: return 0; // 3
    }
}

static void write_ppm(const GBPpu *ppu, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Couldn't open '%s' for writing\n", path);
        return;
    }
    fprintf(f, "P5\n%d %d\n255\n", GB_SCREEN_WIDTH, GB_SCREEN_HEIGHT);
    for (int y = 0; y < GB_SCREEN_HEIGHT; y++) {
        for (int x = 0; x < GB_SCREEN_WIDTH; x++) {
            uint8_t gray = shade_to_gray(ppu->framebuffer[y][x]);
            fwrite(&gray, 1, 1, f);
        }
    }
    fclose(f);
    fprintf(stderr, "Wrote frame to '%s'\n", path);
}

// One scripted button action, timed to a VBlank frame count (the same
// granularity a real player's button presses land on, one screen
// refresh at a time) rather than a raw instruction/T-state count, which
// would be brittle against any timing change elsewhere in the core.
typedef struct InputEvent {
    int frame;
    int is_direction; // 0 = action group (A/B/Select/Start), 1 = direction group
    int button;       // GB_BUTTON_* from joypad.h
    int pressed;       // 1 = press (down), 0 = release (up)
} InputEvent;

static int button_lookup(const char *name, int *is_direction) {
    struct { const char *name; int is_direction; int button; } table[] = {
        {"A", 0, GB_BUTTON_A}, {"B", 0, GB_BUTTON_B},
        {"SELECT", 0, GB_BUTTON_SELECT}, {"START", 0, GB_BUTTON_START},
        {"RIGHT", 1, GB_BUTTON_RIGHT}, {"LEFT", 1, GB_BUTTON_LEFT},
        {"UP", 1, GB_BUTTON_UP}, {"DOWN", 1, GB_BUTTON_DOWN},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcasecmp(name, table[i].name) == 0) {
            *is_direction = table[i].is_direction;
            return table[i].button;
        }
    }
    return -1;
}

// Parses lines of the form "<frame> <BUTTON> <down|up>", e.g.
// "30 START down" then "31 START up" to tap Start on frame 30. Blank
// lines and lines starting with '#' are ignored. Events don't need to
// be in frame order in the file - sorted here (simple insertion sort,
// the event count is always small) so the main loop can just walk the
// array in order.
static int parse_input_script(const char *path, InputEvent *events, int max_events) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Couldn't open input script '%s'\n", path);
        return -1;
    }
    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        char button_name[32], state_name[16];
        int frame;
        if (sscanf(p, "%d %31s %15s", &frame, button_name, state_name) != 3) continue;
        int is_direction;
        int button = button_lookup(button_name, &is_direction);
        if (button < 0) {
            fprintf(stderr, "input script '%s': unknown button '%s'\n", path, button_name);
            fclose(f);
            return -1;
        }
        int pressed;
        if (strcasecmp(state_name, "down") == 0) pressed = 1;
        else if (strcasecmp(state_name, "up") == 0) pressed = 0;
        else {
            fprintf(stderr, "input script '%s': expected 'down'/'up', got '%s'\n", path, state_name);
            fclose(f);
            return -1;
        }
        if (count >= max_events) {
            fprintf(stderr, "input script '%s': too many events (max %d)\n", path, max_events);
            fclose(f);
            return -1;
        }
        int i = count++;
        while (i > 0 && events[i - 1].frame > frame) {
            events[i] = events[i - 1];
            i--;
        }
        events[i] = (InputEvent){frame, is_direction, button, pressed};
    }
    fclose(f);
    return count;
}

static void write_u32le(FILE *f, uint32_t v) { uint8_t b[4] = {(uint8_t)v, (uint8_t)(v>>8), (uint8_t)(v>>16), (uint8_t)(v>>24)}; fwrite(b, 1, 4, f); }
static void write_u16le(FILE *f, uint16_t v) { uint8_t b[2] = {(uint8_t)v, (uint8_t)(v>>8)}; fwrite(b, 1, 2, f); }

// Standard 44-byte-header, 16-bit PCM stereo WAV - the interleaved
// float samples in `samples` (each in [-1,1], as gb_apu_step() fills
// apu->sample_buffer) are the only thing converted; the container
// format itself is plain, well-known, and needs no library.
static void write_wav(const float *samples, int sample_count, int sample_rate, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Couldn't open '%s' for writing\n", path);
        return;
    }
    int frames = sample_count / 2; // stereo pairs
    uint32_t data_bytes = (uint32_t)sample_count * 2; // 16-bit samples
    fwrite("RIFF", 1, 4, f);
    write_u32le(f, 36 + data_bytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    write_u32le(f, 16);
    write_u16le(f, 1); // PCM
    write_u16le(f, 2); // stereo
    write_u32le(f, (uint32_t)sample_rate);
    write_u32le(f, (uint32_t)sample_rate * 2 * 2); // byte rate
    write_u16le(f, 4); // block align
    write_u16le(f, 16); // bits per sample
    fwrite("data", 1, 4, f);
    write_u32le(f, data_bytes);
    for (int i = 0; i < sample_count; i++) {
        double v = samples[i];
        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;
        int16_t s = (int16_t)(v * 32767.0);
        write_u16le(f, (uint16_t)s);
    }
    fclose(f);
    fprintf(stderr, "Wrote %d stereo frames (%.2fs) to '%s'\n", frames, (double)frames / sample_rate, path);
}

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        fprintf(stderr,
                "Usage:\n"
                "  %s <rom.gb>                        Run for a fixed instruction budget\n"
                "  %s <rom.gb> --ppm <out.ppm> [--frames N]\n"
                "                                      Run until N VBlanks complete (default 2),\n"
                "                                      dump the frame as a PPM image, and exit\n"
                "  %s <rom.gb> --wav <out.wav> [--seconds N]\n"
                "                                      Run for N seconds of emulated time (default 5),\n"
                "                                      dump generated audio as a WAV file, and exit\n"
                "  %s <rom.gb> --input <script> [with --ppm/--frames/--wav/--seconds above]\n"
                "                                      Script joypad button presses by VBlank frame\n"
                "                                      number - lines of '<frame> <BUTTON> <down|up>',\n"
                "                                      BUTTON one of A/B/SELECT/START/RIGHT/LEFT/UP/DOWN\n",
                argv[0], argv[0], argv[0], argv[0]);
        return 1;
    }

    const char *ppm_path = NULL;
    const char *wav_path = NULL;
    const char *input_path = NULL;
    int target_frames = 2;
    double wav_seconds = 5.0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--ppm") == 0 && i + 1 < argc) {
            ppm_path = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            target_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--wav") == 0 && i + 1 < argc) {
            wav_path = argv[++i];
        } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input_path = argv[++i];
        } else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            wav_seconds = atof(argv[++i]);
        }
    }

    GBCart cart;
    if (gb_cart_load(&cart, argv[1]) != 0) {
        return 1;
    }

    GBCpu cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.memory = calloc(1, GB_MEM_SIZE); // VRAM/WRAM/OAM/I-O-regs/HRAM only - see cpu.h
    cpu.cart = &cart;

    GBPpu ppu;
    cpu.ppu = &ppu;
    gb_ppu_reset(&ppu);

    GBTimer timer;
    cpu.timer = &timer;
    gb_timer_reset(&timer);

    GBJoypad joypad;
    cpu.joypad = &joypad;
    gb_joypad_reset(&joypad);

    GBApu apu;
    cpu.apu = &apu;
    int sample_cap = wav_path ? (int)(wav_seconds * WAV_SAMPLE_RATE * 2) + 4 : 0;
    float *sample_buffer = sample_cap ? calloc((size_t)sample_cap, sizeof(float)) : NULL;
    gb_apu_reset(&apu, WAV_SAMPLE_RATE, sample_buffer, sample_cap);

    gb_cpu_init_tables();
    gb_cpu_reset(&cpu);
    gb_serial_output_hook = serial_putc;

    InputEvent input_events[MAX_INPUT_EVENTS];
    int input_count = 0;
    if (input_path) {
        input_count = parse_input_script(input_path, input_events, MAX_INPUT_EVENTS);
        if (input_count < 0) return 1;
    }
    int next_input = 0;

    fprintf(stderr, "Starting SM83 execution loop...\n\n");

    // A generous, fixed instruction budget rather than any kind of
    // completion detection - Blargg's test ROMs spin in an infinite
    // loop once done (no clean "exit" signal to detect). 20M
    // instructions is far more than any cpu_instrs sub-test (or a
    // static test image like dmg-acid2) needs to finish.
    const long budget = 20000000;
    long executed = 0;
    int frames_seen = 0;
    for (; executed < budget; executed++) {
        int cycles = gb_cpu_step(&cpu);
        if (cycles < 0) {
            fprintf(stderr, "\nIllegal/unimplemented opcode at PC=0x%04X\n", (unsigned)(cpu.pc - 1));
            break;
        }
        gb_ppu_step(&ppu, &cpu, cycles);
        gb_timer_step(&timer, &cpu, cycles);
        gb_apu_step(&apu, &cpu, cycles);

        if (ppu.frame_ready) {
            ppu.frame_ready = 0;
            frames_seen++;
            while (next_input < input_count && input_events[next_input].frame == frames_seen) {
                InputEvent *ev = &input_events[next_input];
                if (ev->is_direction) gb_joypad_set_direction(&joypad, &cpu, ev->button, ev->pressed);
                else gb_joypad_set_action(&joypad, &cpu, ev->button, ev->pressed);
                next_input++;
            }
            if (ppm_path && frames_seen >= target_frames) break;
        }
        if (wav_path && apu.sample_buffer_len >= sample_cap) break;
    }

    if (ppm_path) {
        write_ppm(&ppu, ppm_path);
    }
    if (wav_path) {
        write_wav(sample_buffer, apu.sample_buffer_len, WAV_SAMPLE_RATE, wav_path);
    }

    fprintf(stderr, "\n\nExecuted %ld instructions (budget %ld), %d frame(s). Final PC=0x%04X\n",
            executed, budget, frames_seen, (unsigned)cpu.pc);

    free(cpu.memory);
    free(sample_buffer);
    gb_cart_free(&cart);
    return 0;
}
