// abc80/emu/src/sound_demo.c - standalone verification tool for
// sound.c: feeds a known, synthetic register-event sequence (matching
// this project's own established pattern - render_demo.c verified
// render.c the identical way, against known input, before it was ever
// trusted against real CPU output) and renders a WAV file, so the VCO
// frequency formula and register-decode logic can be verified (e.g. via
// an FFT) against a known expectation before trusting them against real
// ROM-driven register writes.
//
// Usage: abc80-sound-demo [output.wav] [--live] [--mode NAME]
//   (default output path: /tmp/abc80_sound_demo.wav)
//
// Default sequence (--mode vco, the original/only mode before this
// project's SN76477 subsystems beyond the VCO were implemented), at a
// fabricated 2.9952 MHz clock matching the real ABC80 Z80 clock (see
// abc80/docs/ABC80_ROADMAP.md):
//   0.0s-0.5s: silence (register 0x01 - disabled)
//   0.5s-1.5s: steady VCO tone (register 0x40 - enabled, mixer=VCO,
//              envelope=Mixer Only, vco_mode=fixed - see sound.c's own
//              comment for the exact bit derivation)
//   1.5s-2.0s: silence again
//
// Other --mode values exercise the subsystems this project's own
// research grounded from MAME's real abc80.cpp/sn76477.cpp source
// (abc80/docs/ABC80_REFERENCE.md's Sound section has the derivation and
// expected real timing for each):
//   slf           - SLF oscillator alone (mixer=SLF, envelope=Mixer
//                   Only) - expect a ~3.98Hz on/off toggle, not a tone.
//   vco-swept     - VCO's pitch swept by the SLF (mixer=VCO,
//                   envelope=Mixer Only, vco_mode=swept) - the real
//                   warble/siren effect - expect a dominant frequency
//                   that sweeps over time, not a fixed 640Hz.
//   noise         - noise generator alone (mixer=Noise, envelope=Mixer
//                   Only) - expect broadband/irregular content, not a
//                   single clean frequency.
//   one-shot      - envelope=One-Shot over the VCO tone (mixer=VCO) -
//                   real hardware triggers this on an enabled(bit0=0)
//                   -> disabled(bit0=1) transition (see sound.c's own
//                   comment), so this sequence enables, disables
//                   briefly to trigger, then re-enables - expect a
//                   single ~28.6ms amplitude pulse, not continuous tone.
//   alt-polarity  - envelope=VCO-with-Alternating-Polarity over the VCO
//                   tone (mixer=VCO) - expect an asymmetric ~22ms
//                   attack/~470ms decay amplitude shape, not constant
//                   full-scale.
//
// --live exercises abc80_sound_step_sample() (the same per-sample state
// machine abc80_sound_render_wav() and Milestone 11's real-time
// audio-callback path, abc80/gtk/src/main.c, both drive) via its own
// loop instead of abc80_sound_render_wav()'s event-log-driven one -
// same sequence, same expected 640Hz tone, but generated sample-by-
// sample the way the real SDL audio callback will, so the shared step
// function gets the identical "prove it against known input" check
// abc80_sound_render_wav() already has, independently.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sound.h"

#define ABC80_CLOCK_HZ 2995200.0 // 11.9808 MHz / 2 / 2, see ABC80_ROADMAP.md

// Matches sound.c's own private WAV_SAMPLE_RATE/WAV_AMPLITUDE exactly -
// duplicated here (two numbers, not logic) since sound.c doesn't expose
// its WAV-header-writing internals for reuse, the same "small boilerplate
// constant, not subtle logic" duplication already used elsewhere in this
// project (e.g. ABC80_CLOCK_HZ above, or ABC80_CLOCK_HZ/ABC80_BLINK_HZ in
// abc80/gtk/src/main.c).
#define LIVE_SAMPLE_RATE 44100.0
#define LIVE_AMPLITUDE 8000

static void put_u32le(unsigned char *buf, uint32_t v) {
    buf[0] = (unsigned char)(v & 0xFF);
    buf[1] = (unsigned char)((v >> 8) & 0xFF);
    buf[2] = (unsigned char)((v >> 16) & 0xFF);
    buf[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void put_u16le(unsigned char *buf, uint16_t v) {
    buf[0] = (unsigned char)(v & 0xFF);
    buf[1] = (unsigned char)((v >> 8) & 0xFF);
}

static int render_live_wav(const char *path) {
    double duration_sec = 2.0;
    uint32_t num_samples = (uint32_t)(duration_sec * LIVE_SAMPLE_RATE);

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open '%s' for WAV output: ", path);
        perror(NULL);
        return 0;
    }

    unsigned char header[44];
    memcpy(header, "RIFF", 4);
    put_u32le(header + 4, 36 + num_samples * 2);
    memcpy(header + 8, "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    put_u32le(header + 16, 16);
    put_u16le(header + 20, 1);
    put_u16le(header + 22, 1);
    put_u32le(header + 24, (uint32_t)LIVE_SAMPLE_RATE);
    put_u32le(header + 28, (uint32_t)LIVE_SAMPLE_RATE * 2);
    put_u16le(header + 32, 2);
    put_u16le(header + 34, 16);
    memcpy(header + 36, "data", 4);
    put_u32le(header + 40, num_samples * 2);
    fwrite(header, 1, sizeof(header), f);

    Abc80SoundState state;
    abc80_sound_state_init(&state);
    for (uint32_t i = 0; i < num_samples; i++) {
        double t_sec = (double)i / LIVE_SAMPLE_RATE;
        uint8_t data = (t_sec < 0.5 || t_sec >= 1.5) ? 0x01 : 0x40;
        int16_t sample = abc80_sound_step_sample(&state, data, LIVE_SAMPLE_RATE);
        unsigned char sbuf[2];
        put_u16le(sbuf, (uint16_t)sample);
        fwrite(sbuf, 1, 2, f);
    }

    fclose(f);
    printf("Rendered %u live samples (%.3fs) to '%s'\n", num_samples, duration_sec, path);
    return 1;
}

// Register byte values for each --mode, derived from sound.c's own bit
// layout (bit0 enable/active-low, bit1 vco_voltage, bit2 vco_swept,
// bits3-5 mixer select, bits6-7 envelope select):
#define REG_DISABLED      0x01 // bit0=1
#define REG_VCO_TONE      0x40 // mixer=VCO(0), envelope=Mixer Only(2)
#define REG_SLF_ALONE     0x50 // mixer=SLF(1), envelope=Mixer Only(2)
#define REG_VCO_SWEPT     0x44 // mixer=VCO(0), envelope=Mixer Only(2), vco_mode=swept
#define REG_NOISE_ALONE   0x48 // mixer=Noise(2), envelope=Mixer Only(2)
#define REG_ONE_SHOT_EN   0x80 // mixer=VCO(0), envelope=One-Shot(1), enabled
#define REG_ONE_SHOT_DIS  0x81 // same, but bit0=1 (disabled) - the real trigger edge
#define REG_ALT_POL       0xC0 // mixer=VCO(0), envelope=Alternating-Polarity(3)

int main(int argc, char *argv[]) {
    const char *path = "/tmp/abc80_sound_demo.wav";
    const char *mode = "vco";
    int live = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--live") == 0) {
            live = 1;
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = argv[++i];
        } else {
            path = argv[i];
        }
    }

    printf("VCO frequency (R=%.0f ohm, C=%.0fpF): %.2f Hz\n",
           (double)ABC80_SOUND_VCO_RES_OHMS, ABC80_SOUND_VCO_CAP_FARADS * 1e12,
           abc80_sound_vco_freq_hz());

    if (live) {
        if (!render_live_wav(path)) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    Abc80SoundLog log;
    abc80_sound_log_init(&log);
    double total_sec;

    if (strcmp(mode, "vco") == 0) {
        abc80_sound_write(&log, (uint64_t)(0.0 * ABC80_CLOCK_HZ), REG_DISABLED);
        abc80_sound_write(&log, (uint64_t)(0.5 * ABC80_CLOCK_HZ), REG_VCO_TONE);
        abc80_sound_write(&log, (uint64_t)(1.5 * ABC80_CLOCK_HZ), REG_DISABLED);
        total_sec = 2.0;
    } else if (strcmp(mode, "slf") == 0) {
        abc80_sound_write(&log, (uint64_t)(0.0 * ABC80_CLOCK_HZ), REG_SLF_ALONE);
        total_sec = 2.0;
    } else if (strcmp(mode, "vco-swept") == 0) {
        abc80_sound_write(&log, (uint64_t)(0.0 * ABC80_CLOCK_HZ), REG_VCO_SWEPT);
        total_sec = 2.0;
    } else if (strcmp(mode, "noise") == 0) {
        abc80_sound_write(&log, (uint64_t)(0.0 * ABC80_CLOCK_HZ), REG_NOISE_ALONE);
        total_sec = 1.0;
    } else if (strcmp(mode, "one-shot") == 0) {
        // Real hardware trigger: enable, then a brief disable (the
        // falling-edge trigger), then re-enable so the pulse's amplitude
        // shaping over the VCO tone is actually audible - see this
        // file's own top comment.
        abc80_sound_write(&log, (uint64_t)(0.0 * ABC80_CLOCK_HZ), REG_ONE_SHOT_EN);
        abc80_sound_write(&log, (uint64_t)(0.5 * ABC80_CLOCK_HZ), REG_ONE_SHOT_DIS);
        abc80_sound_write(&log, (uint64_t)(0.501 * ABC80_CLOCK_HZ), REG_ONE_SHOT_EN);
        total_sec = 1.0;
    } else if (strcmp(mode, "alt-polarity") == 0) {
        abc80_sound_write(&log, (uint64_t)(0.0 * ABC80_CLOCK_HZ), REG_ALT_POL);
        total_sec = 1.0;
    } else {
        fprintf(stderr, "Unknown --mode '%s' (expected vco, slf, vco-swept, noise, one-shot, alt-polarity)\n", mode);
        return EXIT_FAILURE;
    }

    uint64_t total_t_states = (uint64_t)(total_sec * ABC80_CLOCK_HZ);
    if (!abc80_sound_render_wav(&log, total_t_states, ABC80_CLOCK_HZ, path)) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
