#ifndef ABC80_SOUND_H
#define ABC80_SOUND_H

#include <stdint.h>

// ABC80's real SN76477 board component values, grounded against MAME's
// src/mame/luxor/abc80.cpp machine_config (fetched from mamedev/mame on
// GitHub - see abc80/docs/ABC80_REFERENCE.md's Sound section for the
// full machine_config block and the derived real timing each produces):
//
//   m_csg->set_noise_params(RES_K(47), RES_K(330), CAP_P(390));
//   m_csg->set_decay_res(RES_K(47));
//   m_csg->set_attack_params(CAP_U(10), RES_K(2.2));
//   m_csg->set_vco_params(0, CAP_N(10), RES_K(100));
//   m_csg->set_slf_params(CAP_U(1), RES_K(220));
//   m_csg->set_oneshot_params(CAP_U(0.1), RES_K(330));
#define ABC80_SOUND_VCO_RES_OHMS            100000.0
#define ABC80_SOUND_VCO_CAP_FARADS          10e-9
#define ABC80_SOUND_SLF_RES_OHMS            220000.0
#define ABC80_SOUND_SLF_CAP_FARADS          1e-6
#define ABC80_SOUND_NOISE_CLOCK_RES_OHMS    47000.0
#define ABC80_SOUND_NOISE_FILTER_RES_OHMS   330000.0
#define ABC80_SOUND_NOISE_FILTER_CAP_FARADS 390e-12
#define ABC80_SOUND_ONE_SHOT_RES_OHMS       330000.0
#define ABC80_SOUND_ONE_SHOT_CAP_FARADS     0.1e-6
#define ABC80_SOUND_ATTACK_RES_OHMS         2200.0
#define ABC80_SOUND_DECAY_RES_OHMS          47000.0
#define ABC80_SOUND_ATTACK_DECAY_CAP_FARADS 10e-6

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

// Persistent per-sample state for every SN76477 subsystem this model
// simulates - one-shot, SLF, VCO, noise (generator + filter), and the
// attack/decay envelope. Each subsystem is an independent RC
// charge/discharge integrator against fixed voltage thresholds (ported
// from MAME's own sn76477_device::sound_stream_update(), the one real
// coupling being VCO-swept-by-SLF mode, where the SLF's own cap voltage
// drives the VCO's charging ceiling) - genuinely history-dependent state,
// not a pure function of absolute time the way a single fixed-frequency
// tone's phase would be. Zero-initialize (abc80_sound_state_init())
// before first use; must persist across calls for a given audio stream
// (one instance per live audio device, or one per abc80_sound_render_wav()
// call) - matches MAME's own device-instance state exactly, including the
// noise generator's 31-bit LFSR starting at 0 (self-seeding via the same
// degenerate-state check MAME uses, not a special case here).
typedef struct {
    double one_shot_cap_voltage;
    int one_shot_running_ff;

    double slf_cap_voltage;
    int slf_out_ff;

    double vco_cap_voltage;
    int vco_out_ff;
    int vco_alt_pos_edge_ff;

    uint32_t noise_rng;
    uint32_t noise_gen_count;
    int real_noise_bit_ff;
    double noise_filter_cap_voltage;
    int filtered_noise_bit_ff;

    double attack_decay_cap_voltage;

    // Tracks bit0 (the enable bit) from the previous call, to detect the
    // real one-shot trigger condition - see sound.c's own comment: MAME's
    // enable_w() fires the one-shot (and resets the attack/decay cap) on
    // an enabled(0)->disabled(1) transition specifically, not on a
    // sustained register value. Initialized to 1 (disabled) so the very
    // first real register write can't spuriously fire a trigger.
    int prev_disabled_bit0;
} Abc80SoundState;

void abc80_sound_state_init(Abc80SoundState *state);

// One sample of real SN76477 output at `data`'s current register state -
// the single implementation both abc80_sound_render_wav() (below) and a
// live real-time audio callback (bin/abc80-gtk) share, driven either by
// a WAV-writing loop over a known duration or indefinitely by an audio
// device. `state` must persist across calls (see its own comment) -
// `sample_rate` only needs to be consistent across calls for a given
// `state`, not any particular fixed value.
//
// Decodes `data`'s mixer/envelope/vco-mode/vco-voltage bits fresh each
// call (see sound.c's own top comment for the exact bit layout) rather
// than storing them separately the way MAME's own per-write setters do -
// equivalent here since the caller always has the current register byte
// on hand for "now" already (the event-log walk in render_wav, or the
// live register straight from port 0x06).
//
// Amplitude is a deliberate simplification, not MAME's exact analog
// output-stage gain-table lookup (out_pos_gain/out_neg_gain,
// center_to_peak_voltage_out()) - consistent with this model's existing
// VCO-as-plain-square-wave choice: full swing during Mixer-Only envelope
// mode (today's only previously-implemented case, unchanged), else the
// swing is linearly scaled by attack_decay_cap_voltage's own 0..1
// fraction of its real range - a genuine, audible attack/decay/one-shot
// shape without porting the analog curve's exact voltages.
int16_t abc80_sound_step_sample(Abc80SoundState *state, uint8_t data, double sample_rate);

// Renders `log` to a 44100Hz mono 16-bit PCM WAV file at `path`,
// spanning real time [0, total_t_states / clock_hz) seconds. Returns 1
// on success, 0 on any error (reported to stderr).
int abc80_sound_render_wav(const Abc80SoundLog *log, uint64_t total_t_states,
                            double clock_hz, const char *path);

#endif
