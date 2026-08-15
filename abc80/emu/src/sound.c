// abc80/emu/src/sound.c - SN76477 Complex Sound Generator (Milestone 5,
// see abc80/docs/ABC80_ROADMAP.md), port 0x06. Grounded against MAME's
// abc80_state::csg_w() (src/mame/luxor/abc80.cpp) for the bit layout:
//
//   bit 0  enable        (active low: 0 = enabled)
//   bit 1  VCO voltage    (0 = 0V "natural" pitch, 1 = 2.5V - saturates
//                          the VCO silent, since 2.5V exceeds the real
//                          chip's VCO_MAX_EXT_VOLTAGE of 2.35V)
//   bit 2  VCO mode       (0 = pitch fixed by VCO voltage above,
//                          1 = pitch swept by the SLF oscillator - a
//                          "warble"/siren effect, not modeled here)
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
// Scoped deliberately, not attempted in full: real SN76477 emulation
// (MAME's own sn76477.cpp) is a genuine per-sample analog simulation of
// four interacting RC-timed subsystems (VCO, SLF, noise generator,
// envelope generator/one-shot) - a large undertaking for what this
// project's own roadmap already calls its lowest-priority, purely
// cosmetic milestone. This implementation only synthesizes real audio
// for the single simplest, most directly useful case - mixer_mode==0
// (VCO alone selected) with envelope_mode==2 (Mixer Only, i.e. the
// output passes through continuously rather than shaped by an attack/
// decay envelope or one-shot pulse) and VCO mode 0 (fixed pitch, not
// SLF-swept) - a steady tone at a fixed pitch, exactly what a BASIC
// SOUND-statement-style beep needs. Any other register combination
// (noise, SLF, one-shot envelopes, alternating polarity, SLF-swept
// warble) produces silence in this model rather than incorrect audio.
//
// The VCO frequency itself isn't guessed either: ABC80's real board
// wires R=100k/C=10nF to the VCO pins (see sound.h's own comment,
// grounded from MAME's machine_config). MAME's own
// sn76477_device::compute_vco_cap_charging_discharging_rate() computes
// the capacitor's charge rate as `0.64 * 2 * VCO_CAP_VOLTAGE_RANGE /
// (R * C)` volts/sec (VCO_CAP_VOLTAGE_RANGE = 2.39V, itself built from
// separately-measured SLF/VCO voltage constants in that same source).
// At the 50%-duty-cycle triangle wave this board's fixed 0V VCO-voltage
// input produces (compute_vco_duty_cycle() returns exactly 0.5 whenever
// vco_voltage <= 0), the charge and discharge halves are symmetric, so
// frequency = rate / (2 * VCO_CAP_VOLTAGE_RANGE) - substituting the rate
// formula above, the VCO_CAP_VOLTAGE_RANGE terms cancel exactly, leaving
// the clean closed form `f = 0.64 / (R * C)` implemented below - derived
// by hand from MAME's own real formula for this project's specific
// constant-voltage case, not copied verbatim (MAME's own code is a
// general per-sample simulation that also handles a continuously
// varying external voltage, which ABC80's software never actually
// drives). For R=100k/C=10nF this gives 640 Hz, verified via
// bin/abc80-sound-demo's FFT check (abc80/docs/ABC80_ROADMAP.md has the
// numbers).
//
// Output waveform: a plain square wave at that frequency, not the SN76477's
// real triangle-wave VCO output run through its (also real, also
// unmodeled) output amplifier stage - a standard, clearly-labeled
// simplification for a recognizable single-tone beep, matching how
// countless simple square-wave "beeper" sound models work in far
// simpler emulators than a full analog simulation would require.

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

// Decodes whether `data` selects the one case this model synthesizes:
// enabled, VCO-only mixer, Mixer-Only envelope, fixed (non-swept) pitch.
// Public (sound.h) since Milestone 11's live-audio callback
// (abc80/gtk/src/main.c) needs the identical gating decode - not a
// second copy of this bit logic.
int abc80_sound_is_steady_vco_tone(uint8_t data) {
    int enabled = !(data & 0x01);
    int vco_mode_fixed = !(data & 0x04); // bit2==0
    int mixer_a = (data >> 4) & 1, mixer_b = (data >> 3) & 1, mixer_c = (data >> 5) & 1;
    int mixer_mode = (mixer_c << 2) | (mixer_b << 1) | mixer_a;
    int envelope_1 = (data >> 7) & 1, envelope_2 = (data >> 6) & 1;
    int envelope_mode = (envelope_2 << 1) | envelope_1;
    return enabled && vco_mode_fixed && (mixer_mode == 0) && (envelope_mode == 2);
}

// See sound.h's own comment - the live-callback counterpart to
// abc80_sound_render_wav()'s per-sample body below, sharing the same
// WAV_AMPLITUDE swing but with an incremental rather than absolute-time
// phase, since a real-time callback runs indefinitely rather than over
// a known, bounded duration.
int16_t abc80_sound_live_sample(uint8_t data, double *phase, double sample_rate) {
    if (!abc80_sound_is_steady_vco_tone(data)) {
        return 0;
    }
    int16_t sample = (*phase < 0.5) ? WAV_AMPLITUDE : -WAV_AMPLITUDE;
    *phase += abc80_sound_vco_freq_hz() / sample_rate;
    if (*phase >= 1.0) {
        *phase -= 1.0;
    }
    return sample;
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

    double vco_freq = abc80_sound_vco_freq_hz();
    int event_index = 0;

    for (uint32_t i = 0; i < num_samples; i++) {
        double t_sec = (double)i / WAV_SAMPLE_RATE;
        uint64_t t_state = (uint64_t)(t_sec * clock_hz);

        while (event_index + 1 < log->count && log->events[event_index + 1].t_state <= t_state) {
            event_index++;
        }
        uint8_t reg = (log->count > 0) ? log->events[event_index].data : 0x01; // default: disabled

        int16_t sample = 0;
        if (log->count > 0 && abc80_sound_is_steady_vco_tone(reg)) {
            double phase = fmod(t_sec * vco_freq, 1.0);
            sample = (int16_t)((phase < 0.5) ? WAV_AMPLITUDE : -WAV_AMPLITUDE);
        }

        unsigned char sbuf[2];
        put_u16le(sbuf, (uint16_t)sample);
        fwrite(sbuf, 1, 2, f);
    }

    fclose(f);
    printf("Rendered %u samples (%.3fs) to '%s'\n", num_samples, duration_sec, path);
    return 1;
}
