// abc80/emu/src/sound_demo.c - standalone verification tool for
// sound.c: feeds a known, synthetic register-event sequence (matching
// this project's own established pattern - render_demo.c verified
// render.c the identical way, against known input, before it was ever
// trusted against real CPU output) and renders a WAV file, so the VCO
// frequency formula and register-decode logic can be verified (e.g. via
// an FFT) against a known expectation before trusting them against real
// ROM-driven register writes.
//
// Usage: abc80-sound-demo [output.wav]  (default: /tmp/abc80_sound_demo.wav)
//
// Sequence rendered (at a fabricated 2.9952 MHz clock, matching the real
// ABC80 Z80 clock - see abc80/docs/ABC80_ROADMAP.md):
//   0.0s-0.5s: silence (register 0x01 - disabled)
//   0.5s-1.5s: steady VCO tone (register 0x40 - enabled, mixer=VCO,
//              envelope=Mixer Only, vco_mode=fixed - see sound.c's own
//              comment for the exact bit derivation)
//   1.5s-2.0s: silence again

#include <stdio.h>
#include <stdlib.h>
#include "sound.h"

#define ABC80_CLOCK_HZ 2995200.0 // 11.9808 MHz / 2 / 2, see ABC80_ROADMAP.md

int main(int argc, char *argv[]) {
    const char *path = (argc > 1) ? argv[1] : "/tmp/abc80_sound_demo.wav";

    Abc80SoundLog log;
    abc80_sound_log_init(&log);

    abc80_sound_write(&log, (uint64_t)(0.0 * ABC80_CLOCK_HZ), 0x01); // disabled
    abc80_sound_write(&log, (uint64_t)(0.5 * ABC80_CLOCK_HZ), 0x40); // VCO tone on
    abc80_sound_write(&log, (uint64_t)(1.5 * ABC80_CLOCK_HZ), 0x01); // disabled

    printf("VCO frequency (R=%.0f ohm, C=%.0fpF): %.2f Hz\n",
           (double)ABC80_SOUND_VCO_RES_OHMS, ABC80_SOUND_VCO_CAP_FARADS * 1e12,
           abc80_sound_vco_freq_hz());

    uint64_t total_t_states = (uint64_t)(2.0 * ABC80_CLOCK_HZ);
    if (!abc80_sound_render_wav(&log, total_t_states, ABC80_CLOCK_HZ, path)) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
