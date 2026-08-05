#ifndef _GB_APU_H
#define _GB_APU_H

#include <stdint.h>

struct GBCpu;

// The four sound channels, the full 0xFF10-0xFF3F register span
// (0xFF10-0xFF26 plus Wave RAM at 0xFF30-0xFF3F, including the real,
// unconnected 0xFF27-0xFF2F gap - see mmu.c). Grounded against pandocs'
// Audio.md/Audio_Registers.md/Audio_details.md (fetched during this
// phase - see docs/GAMEBOY_ROADMAP.md's Phase 5 status for exactly
// what's implemented vs. deliberately deferred): the DIV-APU frame
// sequencer (512 Hz, tied to DIV bit 4's falling edge - the same real-
// hardware-counter approach timer.c already uses for DIV/TIMA, not a
// naive independent counter), CH1's sweep with its shadow register, all
// four channels' waveform generators (pulse duty cycle, wave RAM
// playback, noise LFSR), envelope, length timers (including the
// "extra length clocking on enable" and trigger-time reload-to-63/255
// obscure behaviors - see extra_length_clock_on_enable()/
// trigger_length_reload() in apu.c), DAC on/off, and NR50/51/52 mixing
// (including the documented DMG high-pass filter). Several genuinely
// obscure quirks pandocs' own "Obscure Behavior" section lists (zombie-
// mode volume writes, wave-RAM trigger-time/mid-playback corruption,
// exiting sweep negate mode disabling the channel, LFSR width-switch
// lockup) are NOT modeled - deliberately, and documented as such, not
// silently missing.
typedef struct GBApuChannel {
    int enabled;         // the channel's own generation circuit is active
    int period_timer;    // T-states until the next duty-step/wave-sample/LFSR-clock advance
    int duty_step;        // CH1/CH2: 0-7, index into the selected duty waveform
    int wave_pos;          // CH3: 0-31, index into wave RAM's 32 nibbles
    uint8_t wave_sample_buffer; // CH3: last nibble read - emitted continuously until the next read, per pandocs
    uint16_t lfsr;         // CH4: 15-bit linear feedback shift register (stored in the low 15 bits)

    int length_timer;
    int volume;           // current envelope volume (0-15)
    int envelope_timer;   // T-states... actually DIV-APU ticks until the next envelope step
    int envelope_going;   // whether the envelope is still auto-updating (pace was nonzero at trigger)

    // CH1 only - sweep unit (pandocs' "Pulse channel with sweep" section)
    int sweep_timer;
    int sweep_enabled;
    int sweep_shadow;     // the sweep's own copy of the period, independent of NR13/NR14
} GBApuChannel;

typedef struct GBApu {
    uint8_t nr10, nr11, nr12, nr13, nr14; // CH1
    uint8_t nr21, nr22, nr23, nr24;       // CH2
    uint8_t nr30, nr31, nr32, nr33, nr34; // CH3
    uint8_t wave_ram[16];
    uint8_t nr41, nr42, nr43, nr44;       // CH4
    uint8_t nr50, nr51;
    int enabled; // NR52 bit 7 - the whole APU's power state

    GBApuChannel ch[4]; // indexed 0-3 for CH1-CH4

    int frame_seq_step;      // 0-7, the DIV-APU frame sequencer's own position
    int div_bit4_prev;       // for edge-detecting DIV's bit 4 (see gb_apu_step())

    // Output sample generation - accumulates T-states until the next
    // output sample is due, then mixes the four channels' current
    // analog output through NR50/NR51 and the DMG high-pass filter
    // (pandocs' own cited C snippet, used verbatim) into one stereo
    // sample. output_rate is fixed at construction via gb_apu_reset().
    int output_rate;
    double sample_accum;     // fractional T-states carried over between steps
    double hpf_capacitor_l, hpf_capacitor_r;

    // Ring buffer of generated samples, interleaved stereo, each in
    // [-1, 1] - a driver (main.c) drains this and/or writes it to a WAV
    // file. Sized generously; gb_apu_step() silently stops writing past
    // the end rather than overflowing, since this phase's own bring-up
    // driver only ever needs a few seconds' worth at a time.
    float *sample_buffer;
    int sample_buffer_cap;
    int sample_buffer_len;
} GBApu;

void gb_apu_reset(GBApu *apu, int output_rate, float *sample_buffer, int sample_buffer_cap);

// Advances the APU by `cycles` T-states - called once per gb_cpu_step(),
// same clock/call-site reasoning as gb_ppu_step()/gb_timer_step(). Needs
// `cpu` read-only, purely to read DIV's current value for the frame
// sequencer's edge detection (see gb_timer_read()).
void gb_apu_step(GBApu *apu, struct GBCpu *cpu, int cycles);

uint8_t gb_apu_read(GBApu *apu, uint16_t addr);
void gb_apu_write(GBApu *apu, uint16_t addr, uint8_t val);

#endif
