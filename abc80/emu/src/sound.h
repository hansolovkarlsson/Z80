#ifndef ABC80_SOUND_H
#define ABC80_SOUND_H

#include <stdint.h>

// ABC80's real SN76477 board component values for the VCO oscillator,
// grounded against MAME's src/mame/luxor/abc80.cpp machine_config:
// `m_csg->set_vco_params(0, CAP_N(10), RES_K(100));` - not guessed.
#define ABC80_SOUND_VCO_RES_OHMS   100000.0
#define ABC80_SOUND_VCO_CAP_FARADS 10e-9

// A log of every real change written to port 0x06 (the SN76477 control
// byte - see sound.c's own top comment for the bit layout), timestamped
// in Z80 T-states so the eventual WAV render can reconstruct exactly
// when each state was active. Fixed-size, matching this project's
// existing debug-tool style (chargen_dump.c etc.) rather than dynamic
// allocation - generous enough for many minutes of real register writes.
#define ABC80_SOUND_MAX_EVENTS 65536

typedef struct {
    uint64_t t_state;
    uint8_t data;
} Abc80SoundEvent;

typedef struct {
    Abc80SoundEvent events[ABC80_SOUND_MAX_EVENTS];
    int count;
} Abc80SoundLog;

void abc80_sound_log_init(Abc80SoundLog *log);

// Records a port-0x06 write at T-state `t_state`, if `data` actually
// differs from the log's own last entry (no-op writes of the same byte
// don't need a new event).
void abc80_sound_write(Abc80SoundLog *log, uint64_t t_state, uint8_t data);

// The real VCO tone frequency (Hz) this board's fixed R/C values
// produce when enabled at its natural (non-warbling) pitch - see
// sound.c for the formula and its derivation.
double abc80_sound_vco_freq_hz(void);

// Decodes whether `data` (a real port-0x06 byte) selects the one case
// this model synthesizes real audio for: enabled, VCO-only mixer,
// Mixer-Only envelope, fixed (non-SLF-swept) pitch - see sound.c's own
// top comment for the full bit layout. Every other combination (noise,
// SLF warble, one-shot/alternating-polarity envelopes) is silence in
// this model, not incorrect audio - a documented, deliberate gap.
int abc80_sound_is_steady_vco_tone(uint8_t data);

// One sample of the live, gated square-wave tone at `data`'s current
// register state, for a real-time audio callback (Milestone 11's
// bin/abc80-gtk) rather than abc80_sound_render_wav()'s own offline
// batch rendering below. Advances *phase by vco_freq/sample_rate and
// wraps it mod 1.0 each call - the standard incremental-phase technique
// a live callback needs to stay click-free across buffer boundaries,
// as opposed to render_wav()'s absolute-time-based phase calculation
// (fine for a bounded offline render, not what an indefinitely-running
// callback should do). *phase must persist across calls (owned by the
// caller, typically once per audio device) and starts at 0.0.
int16_t abc80_sound_live_sample(uint8_t data, double *phase, double sample_rate);

// Renders `log` to a 44100Hz mono 16-bit PCM WAV file at `path`,
// spanning real time [0, total_t_states / clock_hz) seconds. Returns 1
// on success, 0 on any error (reported to stderr).
int abc80_sound_render_wav(const Abc80SoundLog *log, uint64_t total_t_states,
                            double clock_hz, const char *path);

#endif
