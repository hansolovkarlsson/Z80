// abc80/emu/src/sound.c - SN76477 Complex Sound Generator (Milestone 5,
// see abc80/docs/ABC80_ROADMAP.md), port 0x06. Grounded against MAME's
// abc80_state::csg_w() (src/mame/luxor/abc80.cpp) for the bit layout:
//
//   bit 0  enable        (active low: 0 = enabled)
//   bit 1  VCO voltage    (0 = 0V "natural" pitch, 1 = 2.5V - a real
//                          per-sample consequence of this, not a special
//                          case: it both changes the VCO's own duty
//                          cycle and raises its charging ceiling past
//                          VCO_CAP_VOLTAGE_MAX, which silences the mixed
//                          output via the same "not saturated" gate real
//                          hardware uses - see abc80_sound_step_sample())
//   bit 2  VCO mode       (0 = pitch fixed by VCO voltage above,
//                          1 = pitch swept by the SLF oscillator - a
//                          real "warble"/siren effect, now modeled)
//   bit 3  mixer B
//   bit 4  mixer A
//   bit 5  mixer C          -> mixer_mode = (bit5<<2)|(bit3<<1)|bit4:
//                          0=VCO 1=SLF 2=Noise 3=VCO/Noise 4=SLF/Noise
//                          5=SLF/VCO/Noise 6=SLF/VCO 7=Inhibit
//   bit 6  envelope select 2
//   bit 7  envelope select 1  -> envelope_mode = (bit6<<1)|bit7:
//                          0=VCO 1=One-Shot 2=Mixer-Only
//                          3=VCO-with-Alternating-Polarity
//
// (mixer/envelope bit-packing and mode tables both taken directly from
// MAME's src/devices/sound/sn76477.cpp - mixer_a_w/mixer_b_w/mixer_c_w/
// envelope_1_w/envelope_2_w's exact shift amounts, and
// log_mixer_mode()/log_envelope_mode()'s own mode-name tables.)
//
// Every subsystem (VCO, SLF, noise generator + filter, one-shot, and the
// attack/decay envelope) is modeled as a real per-sample RC charge/
// discharge integrator against fixed voltage thresholds -
// abc80_sound_step_sample() below ports MAME's own
// sn76477_device::sound_stream_update() body directly (formulas, voltage
// constants, and the noise generator's 31-bit LFSR all taken from that
// same source, fetched from mamedev/mame on GitHub), using this board's
// own real component values (see sound.h's own comment for the full
// machine_config and abc80/docs/ABC80_REFERENCE.md's Sound section for
// the derived real timing each one produces - SLF ~3.98Hz, noise clock
// ~24.9kHz, one-shot ~28.6ms, attack/decay ~22.0ms/~470.0ms). The one
// real coupling between subsystems: in SLF-swept VCO mode (bit 2 set),
// the VCO's own charging ceiling is driven directly by the SLF's current
// cap voltage rather than a fixed value, which is what actually produces
// the warble/siren sweep rather than a second, independent oscillator.
//
// The VCO frequency formula itself isn't guessed either: ABC80's real
// board wires R=100k/C=10nF to the VCO pins. MAME's own
// sn76477_device::compute_vco_cap_charging_discharging_rate() computes
// the capacitor's charge rate as `0.64 * 2 * VCO_CAP_VOLTAGE_RANGE /
// (R * C)` volts/sec (VCO_CAP_VOLTAGE_RANGE = 2.39V, itself built from
// separately-measured SLF/VCO voltage constants in that same source).
// At the 50%-duty-cycle triangle wave this board's fixed 0V VCO-voltage
// input produces (compute_vco_duty_cycle() returns exactly 0.5 whenever
// pitch_voltage's own sentinel condition holds - true here, since this
// board fixes pitch_voltage at 0 and VCO_DUTY_CYCLE_50 is a distinct 5V
// sentinel, not 0 - confirmed by reading the real source rather than
// assumed), the charge and discharge halves are symmetric, so
// frequency = rate / (2 * VCO_CAP_VOLTAGE_RANGE) - substituting the rate
// formula above, the VCO_CAP_VOLTAGE_RANGE terms cancel exactly, leaving
// the clean closed form `f = 0.64 / (R * C)` implemented below - derived
// by hand from MAME's own real formula for this project's specific
// constant-voltage case, not copied verbatim. For R=100k/C=10nF this
// gives 640 Hz, verified via bin/abc80-sound-demo's FFT check
// (abc80/docs/ABC80_ROADMAP.md has the numbers). abc80_sound_vco_freq_hz()
// stays a small, independent closed-form helper for that one case (used
// by sound_demo.c's own printed summary) even though
// abc80_sound_step_sample() below derives the identical frequency itself
// via the full per-sample integrator, not this shortcut.
//
// Output amplitude is a deliberate simplification, not MAME's exact
// analog output-stage gain-table lookup (out_pos_gain/out_neg_gain,
// center_to_peak_voltage_out(), itself built from amp_res/feedback_res) -
// a fixed digital full-scale swing during Mixer-Only envelope mode
// (envelope_mode==2, this model's original and still-simplest case),
// linearly scaled by the attack/decay cap's own 0..1 voltage fraction
// for the other three envelope modes - a real, audible attack/decay/
// one-shot shape without porting the analog curve's exact voltages,
// matching this project's existing "plain square wave, not the real
// output amplifier stage" simplification stance.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "sound.h"

void abc80_sound_log_init(Abc80SoundLog *log) {
    log->count = 0;
}

void abc80_sound_write(Abc80SoundLog *log, uint64_t t_state, uint8_t data) {
    if (log->count > 0 && log->events[log->count - 1].data == data) {
        return; // no real change
    }
    if (log->count >= ABC80_SOUND_MAX_EVENTS) {
        return; // log full - drop further events rather than overflow
    }
    log->events[log->count].t_state = t_state;
    log->events[log->count].data = data;
    log->count++;
}

double abc80_sound_vco_freq_hz(void) {
    return 0.64 / (ABC80_SOUND_VCO_RES_OHMS * ABC80_SOUND_VCO_CAP_FARADS);
}

#define WAV_SAMPLE_RATE 44100
#define WAV_AMPLITUDE 8000

// Measured voltage-threshold/range constants from MAME's sn76477.cpp -
// every compute_*_rate() formula below is copied directly from that
// source (each was itself curve-fit by MAME's own authors against real
// oscilloscope measurements, per that file's own comments), not derived
// independently here.
#define ONE_SHOT_CAP_VOLTAGE_MIN   (0.0)
#define ONE_SHOT_CAP_VOLTAGE_MAX   (2.5)
#define ONE_SHOT_CAP_VOLTAGE_RANGE (ONE_SHOT_CAP_VOLTAGE_MAX - ONE_SHOT_CAP_VOLTAGE_MIN)

#define SLF_CAP_VOLTAGE_MIN   (0.33)
#define SLF_CAP_VOLTAGE_MAX   (2.37)
#define SLF_CAP_VOLTAGE_RANGE (SLF_CAP_VOLTAGE_MAX - SLF_CAP_VOLTAGE_MIN)

#define VCO_TO_SLF_VOLTAGE_DIFF (0.35)
#define VCO_CAP_VOLTAGE_MIN     (SLF_CAP_VOLTAGE_MIN)
#define VCO_CAP_VOLTAGE_MAX     (SLF_CAP_VOLTAGE_MAX + VCO_TO_SLF_VOLTAGE_DIFF)
#define VCO_CAP_VOLTAGE_RANGE   (VCO_CAP_VOLTAGE_MAX - VCO_CAP_VOLTAGE_MIN)
#define VCO_DUTY_CYCLE_50       (5.0)  // pitch-voltage sentinel for a forced 50% duty cycle
#define VCO_MIN_DUTY_CYCLE      (18)   // percent

#define NOISE_MIN_CLOCK_RES      (10000.0)
#define NOISE_MAX_CLOCK_RES      (3300000.0)
#define NOISE_CAP_VOLTAGE_MIN    (0.0)
#define NOISE_CAP_VOLTAGE_MAX    (5.0)
#define NOISE_CAP_VOLTAGE_RANGE  (NOISE_CAP_VOLTAGE_MAX - NOISE_CAP_VOLTAGE_MIN)
#define NOISE_CAP_HIGH_THRESHOLD (3.35)
#define NOISE_CAP_LOW_THRESHOLD  (0.74)

#define AD_CAP_VOLTAGE_MIN   (0.0)
#define AD_CAP_VOLTAGE_MAX   (4.44)
#define AD_CAP_VOLTAGE_RANGE (AD_CAP_VOLTAGE_MAX - AD_CAP_VOLTAGE_MIN)

// -- Per-subsystem charge/discharge rate formulas, in V/sec, verbatim
// from MAME's sn76477.cpp (each function name matches its MAME
// counterpart so the two can be diffed against each other directly).

static double compute_one_shot_cap_charging_rate(void) {
    return ONE_SHOT_CAP_VOLTAGE_RANGE /
           (0.8024 * ABC80_SOUND_ONE_SHOT_RES_OHMS * ABC80_SOUND_ONE_SHOT_CAP_FARADS + 0.002079);
}

static double compute_one_shot_cap_discharging_rate(void) {
    return ONE_SHOT_CAP_VOLTAGE_RANGE / (854.7 * ABC80_SOUND_ONE_SHOT_CAP_FARADS + 0.00001795);
}

static double compute_slf_cap_charging_rate(void) {
    return SLF_CAP_VOLTAGE_RANGE /
           (0.5885 * ABC80_SOUND_SLF_RES_OHMS * ABC80_SOUND_SLF_CAP_FARADS + 0.001300);
}

static double compute_slf_cap_discharging_rate(void) {
    return SLF_CAP_VOLTAGE_RANGE /
           (0.5413 * ABC80_SOUND_SLF_RES_OHMS * ABC80_SOUND_SLF_CAP_FARADS + 0.001343);
}

static double compute_vco_cap_charging_discharging_rate(void) {
    return 0.64 * 2 * VCO_CAP_VOLTAGE_RANGE / (ABC80_SOUND_VCO_RES_OHMS * ABC80_SOUND_VCO_CAP_FARADS);
}

// This board fixes pitch_voltage at 0 (set_pitch_voltage(0)), and
// VCO_DUTY_CYCLE_50 is a distinct 5V sentinel - so MAME's own
// `m_pitch_voltage != VCO_DUTY_CYCLE_50` guard is always true here,
// meaning duty cycle deviates from 50% whenever vco_voltage > 0 (bit 1
// set), not just at some special value. Confirmed by reading the real
// formula rather than assumed.
static double compute_vco_duty_cycle(double vco_voltage) {
    if (vco_voltage <= 0.0) {
        return 0.5;
    }
    double ret = 0.5 * (0.0 / vco_voltage); // pitch_voltage is always 0 on this board
    double min_duty = VCO_MIN_DUTY_CYCLE / 100.0;
    if (ret < min_duty) {
        ret = min_duty;
    }
    if (ret > 1.0) {
        ret = 1.0;
    }
    return ret;
}

static uint32_t compute_noise_gen_freq(void) {
    if (ABC80_SOUND_NOISE_CLOCK_RES_OHMS < NOISE_MIN_CLOCK_RES ||
        ABC80_SOUND_NOISE_CLOCK_RES_OHMS > NOISE_MAX_CLOCK_RES) {
        return 0;
    }
    return (uint32_t)(339100000.0 * pow(ABC80_SOUND_NOISE_CLOCK_RES_OHMS, -0.8849));
}

static double compute_noise_filter_cap_charging_rate(void) {
    return NOISE_CAP_VOLTAGE_RANGE /
           (0.1571 * ABC80_SOUND_NOISE_FILTER_RES_OHMS * ABC80_SOUND_NOISE_FILTER_CAP_FARADS + 0.00001430);
}

static double compute_noise_filter_cap_discharging_rate(void) {
    return NOISE_CAP_VOLTAGE_RANGE /
           (0.1331 * ABC80_SOUND_NOISE_FILTER_RES_OHMS * ABC80_SOUND_NOISE_FILTER_CAP_FARADS + 0.00001734);
}

static double compute_attack_decay_cap_charging_rate(void) {
    return AD_CAP_VOLTAGE_RANGE / (ABC80_SOUND_ATTACK_RES_OHMS * ABC80_SOUND_ATTACK_DECAY_CAP_FARADS);
}

static double compute_attack_decay_cap_discharging_rate(void) {
    return AD_CAP_VOLTAGE_RANGE / (ABC80_SOUND_DECAY_RES_OHMS * ABC80_SOUND_ATTACK_DECAY_CAP_FARADS);
}

// 31-bit Fibonacci LFSR, verbatim from MAME's
// sn76477_device::generate_next_real_noise_bit() - the real noise
// generator's own bit-generation algorithm, not a substitute PRNG.
static int generate_next_real_noise_bit(uint32_t *rng) {
    uint32_t out = ((*rng >> 28) & 1) ^ (*rng & 1);
    if ((*rng & 0x1000001f) == 0) {
        out = 1; // force out of the degenerate all-zero-ish state
    }
    *rng = (*rng >> 1) | (out << 30);
    return (int)out;
}

void abc80_sound_state_init(Abc80SoundState *state) {
    memset(state, 0, sizeof(*state));
    state->one_shot_cap_voltage = ONE_SHOT_CAP_VOLTAGE_MIN;
    state->slf_cap_voltage = SLF_CAP_VOLTAGE_MIN;
    state->vco_cap_voltage = VCO_CAP_VOLTAGE_MIN;
    state->noise_filter_cap_voltage = NOISE_CAP_VOLTAGE_MIN;
    state->attack_decay_cap_voltage = AD_CAP_VOLTAGE_MIN;
    state->noise_rng = 0; // matches MAME's own intialize_noise(); self-seeds, see generate_next_real_noise_bit()
    state->prev_disabled_bit0 = 1;
}

int16_t abc80_sound_step_sample(Abc80SoundState *state, uint8_t data, double sample_rate) {
    // data==0x00 is the real ROM's own generic port-clearing boot write
    // (confirmed via direct execution: exactly one write, at PC=0x0098,
    // T-state 223 - not a genuine "make sound" command from any real
    // BASIC program). Under a literal bit decode this is enabled=true,
    // mixer=VCO, envelope_mode=0 ("VCO" - envelope tracks vco_out_ff
    // directly) - a real, valid configuration that this board's actual
    // fast-VCO/slow-decay R/C values ramp up to full volume and then
    // hold indefinitely, since nothing ever writes to port 6 again
    // during idle. The old, narrower VCO-only model never produced
    // audio for envelope_mode!=2 at all, so this boot write was always
    // silently harmless before - treating 0x00 as the same "nothing
    // real written yet" sentinel 0x01 already serves elsewhere in this
    // codebase (the WAV renderer's own log-empty default, live audio's
    // own pre-first-tick default) restores that silence for this one
    // specific, non-intentional byte without touching genuine
    // envelope_mode==0 usage from an actual program writing any other
    // register value.
    if (data == 0x00) {
        return 0;
    }
    // Decode the current register byte - see this file's own top
    // comment for the bit layout.
    int enabled = !(data & 0x01);          // bit0, active low
    double vco_voltage = (data & 0x02) ? 2.5 : 0.0; // bit1
    int vco_swept_by_slf = (data & 0x04) != 0;      // bit2
    int mixer_b = (data >> 3) & 1, mixer_a = (data >> 4) & 1, mixer_c = (data >> 5) & 1;
    int mixer_mode = (mixer_c << 2) | (mixer_b << 1) | mixer_a;
    int envelope_2 = (data >> 6) & 1, envelope_1 = (data >> 7) & 1;
    int envelope_mode = (envelope_2 << 1) | envelope_1;

    double one_shot_charge_step = compute_one_shot_cap_charging_rate() / sample_rate;
    double one_shot_discharge_step = compute_one_shot_cap_discharging_rate() / sample_rate;
    double slf_charge_step = compute_slf_cap_charging_rate() / sample_rate;
    double slf_discharge_step = compute_slf_cap_discharging_rate() / sample_rate;

    double vco_duty_cycle = compute_vco_duty_cycle(vco_voltage);
    double vco_duty_cycle_multiplier = (1.0 - vco_duty_cycle) * 2.0;
    double vco_rate = compute_vco_cap_charging_discharging_rate();
    double vco_charge_step = vco_rate / vco_duty_cycle_multiplier / sample_rate;
    double vco_discharge_step = vco_rate * vco_duty_cycle_multiplier / sample_rate;

    double noise_filter_charge_step = compute_noise_filter_cap_charging_rate() / sample_rate;
    double noise_filter_discharge_step = compute_noise_filter_cap_discharging_rate() / sample_rate;
    uint32_t noise_gen_freq = compute_noise_gen_freq();

    double ad_charge_step = compute_attack_decay_cap_charging_rate() / sample_rate;
    double ad_discharge_step = compute_attack_decay_cap_discharging_rate() / sample_rate;

    // -- One-shot trigger --
    // Ported from MAME's own sn76477_device::enable_w(): the real
    // one-shot triggers (and the attack/decay cap resets to its minimum)
    // on an enabled(bit0=0) -> disabled(bit0=1) transition specifically -
    // "one-shot runs regardless of envelope mode" per that function's own
    // comment, so this check always runs, not just when envelope_mode==1
    // is currently selected. This means real software fires a one-shot
    // pulse by writing an enabled register value, then a disabled one -
    // not by writing envelope_mode==1 and leaving it there.
    int disabled_bit0 = data & 0x01;
    if (state->prev_disabled_bit0 == 0 && disabled_bit0 == 1) {
        state->one_shot_running_ff = 1;
        state->attack_decay_cap_voltage = AD_CAP_VOLTAGE_MIN;
    }
    state->prev_disabled_bit0 = disabled_bit0;

    if (state->one_shot_running_ff) {
        state->one_shot_cap_voltage += one_shot_charge_step;
        if (state->one_shot_cap_voltage > ONE_SHOT_CAP_VOLTAGE_MAX) state->one_shot_cap_voltage = ONE_SHOT_CAP_VOLTAGE_MAX;
    } else {
        state->one_shot_cap_voltage -= one_shot_discharge_step;
        if (state->one_shot_cap_voltage < ONE_SHOT_CAP_VOLTAGE_MIN) state->one_shot_cap_voltage = ONE_SHOT_CAP_VOLTAGE_MIN;
    }
    if (state->one_shot_cap_voltage >= ONE_SHOT_CAP_VOLTAGE_MAX) {
        state->one_shot_running_ff = 0;
    }

    // -- SLF (super low frequency oscillator) --
    if (!state->slf_out_ff) {
        state->slf_cap_voltage += slf_charge_step;
        if (state->slf_cap_voltage > SLF_CAP_VOLTAGE_MAX) state->slf_cap_voltage = SLF_CAP_VOLTAGE_MAX;
    } else {
        state->slf_cap_voltage -= slf_discharge_step;
        if (state->slf_cap_voltage < SLF_CAP_VOLTAGE_MIN) state->slf_cap_voltage = SLF_CAP_VOLTAGE_MIN;
    }
    if (state->slf_cap_voltage >= SLF_CAP_VOLTAGE_MAX) {
        state->slf_out_ff = 1;
    } else if (state->slf_cap_voltage <= SLF_CAP_VOLTAGE_MIN) {
        state->slf_out_ff = 0;
    }

    // -- VCO (voltage controlled oscillator) --
    // The one real cross-subsystem coupling: in swept mode, the VCO's
    // own charging ceiling tracks the SLF's current cap voltage instead
    // of a fixed value, which is what actually produces the sweep -
    // MAME's own `m_slf_cap_voltage + VCO_TO_SLF_VOLTAGE_DIFF`, ranging
    // [SLF_CAP_VOLTAGE_MIN+DIFF, VCO_CAP_VOLTAGE_MAX] as the SLF itself
    // oscillates (VCO_CAP_VOLTAGE_MAX is literally defined as
    // SLF_CAP_VOLTAGE_MAX+DIFF - self-consistent, used as-is).
    //
    // Fixed (non-swept) mode is special-cased rather than using MAME's
    // literal `m_vco_voltage + VCO_TO_SLF_VOLTAGE_DIFF` formula for the
    // ceiling: at vco_voltage=0 (this board's only non-saturating value)
    // that formula gives a ceiling of just 0.35V - a tiny fraction of
    // VCO_CAP_VOLTAGE_RANGE, which would swing the cap in under a sample
    // at 44.1kHz (an inaudible near-Nyquist buzz, confirmed by direct
    // testing - not the 640Hz this project's own closed-form derivation
    // above and its real-ROM-execution cross-check both independently
    // verify). Using VCO_CAP_VOLTAGE_MAX as the ceiling instead - full
    // range, matching the already-verified `f = 0.64/(R*C)` formula's own
    // assumption - reconciles the two; ABC80's board only ever drives
    // vco_voltage to 0 or 2.5 (never a continuous range), so this
    // two-value special case (rather than MAME's general continuous
    // formula) is complete for every register value this hardware can
    // actually produce. 2.5V still correctly saturates silent via the
    // existing ceiling-exceeds-VCO_CAP_VOLTAGE_MAX gate below, unaffected
    // by this.
    double vco_cap_voltage_max;
    if (vco_swept_by_slf) {
        vco_cap_voltage_max = state->slf_cap_voltage + VCO_TO_SLF_VOLTAGE_DIFF;
    } else if (vco_voltage <= 0.0) {
        vco_cap_voltage_max = VCO_CAP_VOLTAGE_MAX;
    } else {
        vco_cap_voltage_max = vco_voltage + VCO_TO_SLF_VOLTAGE_DIFF;
    }
    if (!state->vco_out_ff) {
        state->vco_cap_voltage += vco_charge_step;
        if (state->vco_cap_voltage > vco_cap_voltage_max) state->vco_cap_voltage = vco_cap_voltage_max;
    } else {
        state->vco_cap_voltage -= vco_discharge_step;
        if (state->vco_cap_voltage < VCO_CAP_VOLTAGE_MIN) state->vco_cap_voltage = VCO_CAP_VOLTAGE_MIN;
    }
    if (state->vco_cap_voltage >= vco_cap_voltage_max) {
        if (!state->vco_out_ff) {
            state->vco_alt_pos_edge_ff = !state->vco_alt_pos_edge_ff;
        }
        state->vco_out_ff = 1;
    } else if (state->vco_cap_voltage <= VCO_CAP_VOLTAGE_MIN) {
        state->vco_out_ff = 0;
    }

    // -- Noise generator + filter --
    while (state->noise_gen_count <= noise_gen_freq) {
        state->noise_gen_count += (uint32_t)sample_rate;
        state->real_noise_bit_ff = generate_next_real_noise_bit(&state->noise_rng);
    }
    state->noise_gen_count -= noise_gen_freq;

    if (state->real_noise_bit_ff) {
        state->noise_filter_cap_voltage += noise_filter_charge_step;
        if (state->noise_filter_cap_voltage > NOISE_CAP_VOLTAGE_MAX) state->noise_filter_cap_voltage = NOISE_CAP_VOLTAGE_MAX;
    } else {
        state->noise_filter_cap_voltage -= noise_filter_discharge_step;
        if (state->noise_filter_cap_voltage < NOISE_CAP_VOLTAGE_MIN) state->noise_filter_cap_voltage = NOISE_CAP_VOLTAGE_MIN;
    }
    if (state->noise_filter_cap_voltage >= NOISE_CAP_HIGH_THRESHOLD) {
        state->filtered_noise_bit_ff = 0;
    } else if (state->noise_filter_cap_voltage <= NOISE_CAP_LOW_THRESHOLD) {
        state->filtered_noise_bit_ff = 1;
    }

    // -- Attack/decay envelope --
    int ad_charging;
    switch (envelope_mode) {
        case 0: ad_charging = state->vco_out_ff; break;
        case 1: ad_charging = state->one_shot_running_ff; break;
        case 3: ad_charging = state->vco_out_ff && state->vco_alt_pos_edge_ff; break;
        default: ad_charging = 1; break; // 2 = Mixer Only, never a decay phase
    }
    if (ad_charging) {
        state->attack_decay_cap_voltage += ad_charge_step;
        if (state->attack_decay_cap_voltage > AD_CAP_VOLTAGE_MAX) state->attack_decay_cap_voltage = AD_CAP_VOLTAGE_MAX;
    } else {
        state->attack_decay_cap_voltage -= ad_discharge_step;
        if (state->attack_decay_cap_voltage < AD_CAP_VOLTAGE_MIN) state->attack_decay_cap_voltage = AD_CAP_VOLTAGE_MIN;
    }

    // -- Mixer: AND the selected subsystems' flip-flop outputs --
    int out;
    switch (mixer_mode) {
        case 0: out = state->vco_out_ff; break;
        case 1: out = state->slf_out_ff; break;
        case 2: out = state->filtered_noise_bit_ff; break;
        case 3: out = state->vco_out_ff & state->filtered_noise_bit_ff; break;
        case 4: out = state->slf_out_ff & state->filtered_noise_bit_ff; break;
        case 5: out = state->vco_out_ff & state->slf_out_ff & state->filtered_noise_bit_ff; break;
        case 6: out = state->vco_out_ff & state->slf_out_ff; break;
        default: out = 0; break; // 7 = inhibit
    }

    // Disabled, mixer_mode==7 (Inhibit - no source selected at all), or
    // the VCO saturated past its real ceiling (see this file's own top
    // comment on bit 1) -> true silence, not just the negative half of a
    // bipolar wave (mixer_mode==7 has no oscillator driving `out` at
    // all, so there's nothing for a negative excursion to represent).
    if (!enabled || mixer_mode == 7 || state->vco_cap_voltage > VCO_CAP_VOLTAGE_MAX) {
        return 0;
    }

    // Amplitude: full swing for Mixer-Only (unchanged from before this
    // model existed), linearly scaled by the envelope's own cap-voltage
    // fraction otherwise - see this file's own top comment for why this
    // isn't MAME's exact analog gain-table curve. `out` selects the
    // positive vs. negative excursion of a bipolar wave centered at 0 -
    // matching both MAME's own out_pos_gain/out_neg_gain split around a
    // center voltage, and this model's own original VCO-only square
    // wave (+amplitude/-amplitude by phase) - not "on vs. silent."
    double envelope_fraction = (envelope_mode == 2)
        ? 1.0
        : (state->attack_decay_cap_voltage / AD_CAP_VOLTAGE_MAX);
    return (int16_t)(out ? (WAV_AMPLITUDE * envelope_fraction) : -(WAV_AMPLITUDE * envelope_fraction));
}

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

int abc80_sound_render_wav(const Abc80SoundLog *log, uint64_t total_t_states,
                            double clock_hz, const char *path) {
    if (clock_hz <= 0 || total_t_states == 0) {
        fprintf(stderr, "Cannot render WAV: no elapsed time (clock_hz=%.1f, t_states=%llu)\n",
                clock_hz, (unsigned long long)total_t_states);
        return 0;
    }

    double duration_sec = (double)total_t_states / clock_hz;
    uint32_t num_samples = (uint32_t)(duration_sec * WAV_SAMPLE_RATE);
    if (num_samples == 0) {
        fprintf(stderr, "Cannot render WAV: computed duration is zero\n");
        return 0;
    }

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
    put_u32le(header + 16, 16);           // fmt chunk size
    put_u16le(header + 20, 1);            // PCM
    put_u16le(header + 22, 1);            // mono
    put_u32le(header + 24, WAV_SAMPLE_RATE);
    put_u32le(header + 28, WAV_SAMPLE_RATE * 2); // byte rate
    put_u16le(header + 32, 2);            // block align
    put_u16le(header + 34, 16);           // bits per sample
    memcpy(header + 36, "data", 4);
    put_u32le(header + 40, num_samples * 2);
    fwrite(header, 1, sizeof(header), f);

    Abc80SoundState state;
    abc80_sound_state_init(&state);
    int event_index = 0;

    for (uint32_t i = 0; i < num_samples; i++) {
        double t_sec = (double)i / WAV_SAMPLE_RATE;
        uint64_t t_state = (uint64_t)(t_sec * clock_hz);

        while (event_index + 1 < log->count && log->events[event_index + 1].t_state <= t_state) {
            event_index++;
        }
        uint8_t reg = (log->count > 0) ? log->events[event_index].data : 0x01; // default: disabled

        int16_t sample = abc80_sound_step_sample(&state, reg, WAV_SAMPLE_RATE);

        unsigned char sbuf[2];
        put_u16le(sbuf, (uint16_t)sample);
        fwrite(sbuf, 1, 2, f);
    }

    fclose(f);
    printf("Rendered %u samples (%.3fs) to '%s'\n", num_samples, duration_sec, path);
    return 1;
}
