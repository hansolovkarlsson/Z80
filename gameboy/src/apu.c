#include "apu.h"
#include "cpu.h"
#include <string.h>
#include <math.h>

// The classic 8-step-per-period duty waveforms, read out MSB-first
// (bit 7 = duty_step 0). Pandocs' Audio_Registers.md gives the duty
// percentages and a rendered waveform image, not raw bit patterns -
// these specific byte values are the same ones used across the wider
// GB emulator community and reproduce the documented 12.5/25/50/75%
// ratios exactly; exact phase (which edge of the cycle is "high"
// first) isn't independently re-derived from a primary source, since
// it doesn't affect any of this phase's correctness gates (Blargg's
// dmg_sound tests check register/timing behavior, not waveform shape).
static const uint8_t duty_table[4] = {0x01, 0x81, 0x87, 0x7E};

// CH4's clock divisor, indexed by NR43's 3-bit "clock divider" field -
// derived directly from pandocs' own formula (frequency =
// 262144/(divider*2^shift) Hz, with divider=0 treated as 0.5): T-states
// between LFSR clocks = 16*divider (with 0 treated as 8), then <<shift.
static const int noise_divisor_table[8] = {8, 16, 32, 48, 64, 80, 96, 112};

static int dac_enabled(GBApu *apu, int ch) {
    switch (ch) {
        case 0: return (apu->nr12 & 0xF8) != 0;
        case 1: return (apu->nr22 & 0xF8) != 0;
        case 2: return (apu->nr30 & 0x80) != 0;
        default: return (apu->nr42 & 0xF8) != 0; // 3
    }
}

// pandocs' "Pulse channel with sweep" section: frequency calculation
// reads the sweep's own shadow copy of the period (not NR13/NR14
// directly), shifts right by the individual step, and adds or
// subtracts that from the shadow value depending on NR10's direction
// bit - shared by the immediate trigger-time check and the periodic
// 128 Hz sweep tick below, since both compute it identically.
static int sweep_calc(GBApu *apu) {
    GBApuChannel *c = &apu->ch[0];
    int step = apu->nr10 & 0x07;
    int delta = c->sweep_shadow >> step;
    return (apu->nr10 & 0x08) ? (c->sweep_shadow - delta) : (c->sweep_shadow + delta);
}

// Both trigger's own zero-reload and the write-time "extra clock" quirk
// below key off the same condition: whether the frame sequencer's next
// step (apu->frame_seq_step, the step that will run on the *next*
// DIV-APU edge) is one that clocks length (even steps, per
// frame_sequencer_step() below).
static int next_seq_step_clocks_length(GBApu *apu) {
    return (apu->frame_seq_step % 2) == 0;
}

// pandocs' Audio_details.md "Obscure Behavior": triggering a channel
// while length is enabled, with the length timer already at 0, normally
// reloads it to max - but if the next frame-sequencer step wouldn't have
// clocked length anyway, that reload immediately "eats" one tick, same
// as the write-time quirk below, landing one below max instead.
static int trigger_length_reload(GBApu *apu, uint8_t nrx4_val, int max) {
    if (!next_seq_step_clocks_length(apu) && (nrx4_val & 0x40)) return max - 1;
    return max;
}

static void trigger_ch1(GBApu *apu) {
    GBApuChannel *c = &apu->ch[0];
    c->enabled = dac_enabled(apu, 0);
    if (c->length_timer == 0) c->length_timer = trigger_length_reload(apu, apu->nr14, 64);
    int period = apu->nr13 | ((apu->nr14 & 0x07) << 8);
    c->period_timer = (2048 - period) * 4;
    c->envelope_timer = apu->nr12 & 0x07;
    c->envelope_going = (apu->nr12 & 0x07) != 0;
    c->volume = (apu->nr12 >> 4) & 0x0F;

    c->sweep_shadow = period;
    c->sweep_timer = (apu->nr10 >> 4) & 0x07;
    if (c->sweep_timer == 0) c->sweep_timer = 8; // "a period of 0 is treated as 8"
    int step = apu->nr10 & 0x07;
    c->sweep_enabled = (((apu->nr10 >> 4) & 0x07) != 0) || (step != 0);
    if (step != 0 && sweep_calc(apu) > 0x7FF) c->enabled = 0;
}

static void trigger_ch2(GBApu *apu) {
    GBApuChannel *c = &apu->ch[1];
    c->enabled = dac_enabled(apu, 1);
    if (c->length_timer == 0) c->length_timer = trigger_length_reload(apu, apu->nr24, 64);
    int period = apu->nr23 | ((apu->nr24 & 0x07) << 8);
    c->period_timer = (2048 - period) * 4;
    c->envelope_timer = apu->nr22 & 0x07;
    c->envelope_going = (apu->nr22 & 0x07) != 0;
    c->volume = (apu->nr22 >> 4) & 0x0F;
}

static void trigger_ch3(GBApu *apu) {
    GBApuChannel *c = &apu->ch[2];
    c->enabled = dac_enabled(apu, 2);
    if (c->length_timer == 0) c->length_timer = trigger_length_reload(apu, apu->nr34, 256);
    int period = apu->nr33 | ((apu->nr34 & 0x07) << 8);
    c->period_timer = (2048 - period) * 2;
    c->wave_pos = 0; // "reset, but its *not* refilled" - wave_sample_buffer keeps its last value
}

static void trigger_ch4(GBApu *apu) {
    GBApuChannel *c = &apu->ch[3];
    c->enabled = dac_enabled(apu, 3);
    if (c->length_timer == 0) c->length_timer = trigger_length_reload(apu, apu->nr44, 64);
    c->envelope_timer = apu->nr42 & 0x07;
    c->envelope_going = (apu->nr42 & 0x07) != 0;
    c->volume = (apu->nr42 >> 4) & 0x0F;
    c->lfsr = 0; // pandocs' Audio_details.md: "The LFSR is set to 0 when (re)triggering the channel"
}

void gb_apu_reset(GBApu *apu, int output_rate, float *sample_buffer, int sample_buffer_cap) {
    memset(apu, 0, sizeof(*apu));
    apu->output_rate = output_rate;
    apu->sample_buffer = sample_buffer;
    apu->sample_buffer_cap = sample_buffer_cap;

    // Real DMG post-boot register values (pandocs' Power_Up_Sequence.md,
    // same page Phase 1/2/3 already cited for CPU/PPU reset state) -
    // NR52=$F1 reflects the boot ROM having actually used CH1 to play
    // its own startup chime, not an arbitrary default.
    apu->enabled = 1;
    apu->nr50 = 0x77;
    apu->nr51 = 0xF3;
    apu->nr10 = 0x80;
    apu->nr11 = 0xBF;
    apu->nr12 = 0xF3;
    apu->nr14 = 0xBF;
    apu->nr21 = 0x3F;
    apu->nr24 = 0xBF;
    apu->nr30 = 0x7F;
    apu->nr31 = 0xFF;
    apu->nr32 = 0x9F;
    apu->nr34 = 0xBF;
    apu->nr41 = 0xFF;
    apu->nr44 = 0xBF;
    apu->ch[0].enabled = 1;
}

// --- Per-T-state channel generation ---

static void tick_pulse(GBApuChannel *c, uint8_t nrx3, uint8_t nrx4) {
    if (c->period_timer > 0) { c->period_timer--; return; }
    int period = nrx3 | ((nrx4 & 0x07) << 8);
    c->period_timer = (2048 - period) * 4;
    c->duty_step = (c->duty_step + 1) & 0x07;
}

static void tick_wave(GBApu *apu) {
    GBApuChannel *c = &apu->ch[2];
    if (c->period_timer > 0) { c->period_timer--; return; }
    int period = apu->nr33 | ((apu->nr34 & 0x07) << 8);
    c->period_timer = (2048 - period) * 2;
    c->wave_pos = (c->wave_pos + 1) & 0x1F;
    uint8_t byte = apu->wave_ram[c->wave_pos / 2];
    c->wave_sample_buffer = (c->wave_pos % 2 == 0) ? (byte >> 4) : (byte & 0x0F);
}

// pandocs' Audio_details.md "Noise channel (CH4)": the LFSR is modeled
// as a genuine 16-bit register even though only 15 bits are ever read
// back - bit 15 is scratch space for the newly-computed XNOR result
// before the whole thing shifts right, which (in short/7-bit mode)
// also gets mirrored into bit 7 *before* that same shift.
static void tick_noise(GBApu *apu) {
    GBApuChannel *c = &apu->ch[3];
    int shift = (apu->nr43 >> 4) & 0x0F;
    if (shift >= 14) return; // "stops the channel from being clocked entirely"
    if (c->period_timer > 0) { c->period_timer--; return; }
    int divisor = noise_divisor_table[apu->nr43 & 0x07];
    c->period_timer = divisor << shift;

    int bit0 = c->lfsr & 1;
    int bit1 = (c->lfsr >> 1) & 1;
    int xnor = (bit0 == bit1) ? 1 : 0;

    c->lfsr = (uint16_t)((c->lfsr & ~(1 << 15)) | (xnor << 15));
    if (apu->nr43 & 0x08) { // short (7-bit) mode
        c->lfsr = (uint16_t)((c->lfsr & ~(1 << 7)) | (xnor << 7));
    }
    c->lfsr >>= 1;
}

// --- DIV-APU frame sequencer (envelope/length/sweep) ---

static uint8_t nrx2_for(GBApu *apu, int ch) {
    switch (ch) {
        case 0: return apu->nr12;
        case 1: return apu->nr22;
        case 3: return apu->nr42;
        default: return 0; // CH3 has no envelope
    }
}

static uint8_t nrx4_for(GBApu *apu, int ch) {
    switch (ch) {
        case 0: return apu->nr14;
        case 1: return apu->nr24;
        case 2: return apu->nr34;
        default: return apu->nr44; // 3
    }
}

static void tick_length(GBApu *apu, int ch) {
    GBApuChannel *c = &apu->ch[ch];
    if (!(nrx4_for(apu, ch) & 0x40)) return;
    if (c->length_timer > 0) {
        c->length_timer--;
        if (c->length_timer == 0) c->enabled = 0;
    }
}

static void tick_envelope(GBApu *apu, int ch) {
    GBApuChannel *c = &apu->ch[ch];
    if (!c->envelope_going) return;
    int pace = nrx2_for(apu, ch) & 0x07;
    if (pace == 0) return;
    if (c->envelope_timer > 0) c->envelope_timer--;
    if (c->envelope_timer == 0) {
        c->envelope_timer = pace;
        int dir = (nrx2_for(apu, ch) & 0x08) ? 1 : -1;
        int new_vol = c->volume + dir;
        if (new_vol >= 0 && new_vol <= 15) c->volume = new_vol;
        else c->envelope_going = 0; // stops auto-updating once it hits 0 or 15
    }
}

static void tick_sweep(GBApu *apu) {
    GBApuChannel *c = &apu->ch[0];
    if (c->sweep_timer > 0) c->sweep_timer--;
    if (c->sweep_timer != 0) return;

    c->sweep_timer = (apu->nr10 >> 4) & 0x07;
    if (c->sweep_timer == 0) c->sweep_timer = 8;

    if (!c->sweep_enabled || ((apu->nr10 >> 4) & 0x07) == 0) return;

    int new_freq = sweep_calc(apu);
    if (new_freq > 0x7FF) { c->enabled = 0; return; }
    if ((apu->nr10 & 0x07) != 0) {
        c->sweep_shadow = new_freq;
        apu->nr13 = (uint8_t)(new_freq & 0xFF);
        apu->nr14 = (uint8_t)((apu->nr14 & 0xF8) | ((new_freq >> 8) & 0x07));
        // A second calculation+overflow check runs immediately - per
        // pandocs, its result is only used to (maybe) disable the
        // channel, never written back.
        if (sweep_calc(apu) > 0x7FF) c->enabled = 0;
    }
}

// The 8-step, 512 Hz sequence: length ticks every other step (256 Hz),
// sweep every 4th (128 Hz, steps 2 and 6), envelope every 8th (64 Hz,
// step 7 only) - the standard mapping, self-consistent with the exact
// rates pandocs' Audio_details.md DIV-APU table gives (2/4/8 out of 8).
static void frame_sequencer_step(GBApu *apu) {
    int step = apu->frame_seq_step;
    if (step % 2 == 0) {
        for (int i = 0; i < 4; i++) tick_length(apu, i);
    }
    if (step == 2 || step == 6) tick_sweep(apu);
    if (step == 7) {
        for (int i = 0; i < 4; i++) tick_envelope(apu, i);
    }
    apu->frame_seq_step = (step + 1) & 0x07;
}

// --- Mixing ---

// Digital 0-15 -> analog +1..-1 (negative slope: digital 0 maps to
// analog +1) - pandocs' Audio_details.md "DACs" section, cited
// precisely since the negative slope is easy to get backwards.
static double channel_analog(GBApu *apu, int ch) {
    GBApuChannel *c = &apu->ch[ch];
    if (!c->enabled || !dac_enabled(apu, ch)) return 0.0;

    int digital;
    switch (ch) {
        case 0: {
            int bit = (duty_table[(apu->nr11 >> 6) & 0x03] >> (7 - c->duty_step)) & 1;
            digital = bit ? c->volume : 0;
            break;
        }
        case 1: {
            int bit = (duty_table[(apu->nr21 >> 6) & 0x03] >> (7 - c->duty_step)) & 1;
            digital = bit ? c->volume : 0;
            break;
        }
        case 2: {
            int level = (apu->nr32 >> 5) & 0x03;
            if (level == 0) return 0.0; // mute
            digital = c->wave_sample_buffer >> (level - 1);
            break;
        }
        default: // 3 - noise: "if the bit [after shifting] is 0, emit 0; otherwise, the volume"
            digital = (c->lfsr & 1) ? c->volume : 0;
            break;
    }
    return 1.0 - (digital / 7.5);
}

static void generate_sample_if_due(GBApu *apu) {
    apu->sample_accum += 1.0;
    double t_states_per_sample = 4194304.0 / apu->output_rate;
    if (apu->sample_accum < t_states_per_sample) return;
    apu->sample_accum -= t_states_per_sample;

    double ch_out[4];
    for (int i = 0; i < 4; i++) ch_out[i] = channel_analog(apu, i);

    double left = 0.0, right = 0.0;
    if (apu->enabled) {
        for (int i = 0; i < 4; i++) {
            if (apu->nr51 & (1 << (i + 4))) left += ch_out[i];
            if (apu->nr51 & (1 << i)) right += ch_out[i];
        }
        left *= (((apu->nr50 >> 4) & 0x07) + 1) / 8.0;
        right *= ((apu->nr50 & 0x07) + 1) / 8.0;
    }

    // DMG high-pass filter - pandocs' Audio_details.md "Obscure
    // Behavior" section gives this exact algorithm (its own C snippet,
    // reproduced here rather than re-derived) and the charge factor
    // formula for an arbitrary output rate.
    int any_dac_on = dac_enabled(apu, 0) || dac_enabled(apu, 1) || dac_enabled(apu, 2) || dac_enabled(apu, 3);
    double charge_factor = pow(0.999958, 4194304.0 / apu->output_rate);
    double out_left = 0.0, out_right = 0.0;
    if (apu->enabled && any_dac_on) {
        out_left = left - apu->hpf_capacitor_l;
        apu->hpf_capacitor_l = left - out_left * charge_factor;
        out_right = right - apu->hpf_capacitor_r;
        apu->hpf_capacitor_r = right - out_right * charge_factor;
    }

    if (apu->sample_buffer && apu->sample_buffer_len + 2 <= apu->sample_buffer_cap) {
        // Each channel contributes up to +-1, so a sum of up to 4 is
        // divided back down to keep the final output within +-1.
        apu->sample_buffer[apu->sample_buffer_len++] = (float)(out_left / 4.0);
        apu->sample_buffer[apu->sample_buffer_len++] = (float)(out_right / 4.0);
    }
}

void gb_apu_step(GBApu *apu, struct GBCpu *cpu, int cycles) {
    for (int i = 0; i < cycles; i++) {
        // DIV-APU: tied to DIV's own bit 4 falling edge (pandocs'
        // Audio_details.md), not an independent counter - the same
        // real-hardware system-counter approach timer.c already uses,
        // so a program that speeds this up by writing DIV (a real,
        // if unusual, technique) is handled correctly for free.
        uint8_t div = gb_read_byte(cpu, 0xFF04);
        int bit4 = (div >> 4) & 1;
        // Frozen while powered off, not just silenced - confirmed
        // against Blargg's dmg_sound 08-len ctr during power.gb (its
        // own source comment: length counters are "not clocked" while
        // off, on DMG). gb_apu_write()'s NR52 handler deliberately
        // leaves the NRx1 length-timer registers unzeroed on power-off
        // for the same reason: the *value* survives the power cycle,
        // it just doesn't count down during it.
        if (apu->enabled && apu->div_bit4_prev == 1 && bit4 == 0) frame_sequencer_step(apu);
        apu->div_bit4_prev = bit4;

        if (apu->enabled) {
            tick_pulse(&apu->ch[0], apu->nr13, apu->nr14);
            tick_pulse(&apu->ch[1], apu->nr23, apu->nr24);
            tick_wave(apu);
            tick_noise(apu);
        }

        generate_sample_if_due(apu);
    }
}

// --- Registers ---

uint8_t gb_apu_read(GBApu *apu, uint16_t addr) {
    if (addr >= 0xFF30 && addr <= 0xFF3F) return apu->wave_ram[addr - 0xFF30];

    // Unused/write-only bits read back as 1 - the standard OR-mask
    // convention, derived here directly from each register's own R/W
    // bit descriptions above rather than assumed: any bit documented
    // as "write-only" or simply not listed is forced high on read.
    switch (addr) {
        case 0xFF10: return (uint8_t)(apu->nr10 | 0x80);
        case 0xFF11: return (uint8_t)(apu->nr11 | 0x3F);
        case 0xFF12: return apu->nr12;
        case 0xFF13: return 0xFF;
        case 0xFF14: return (uint8_t)(apu->nr14 | 0xBF);
        case 0xFF16: return (uint8_t)(apu->nr21 | 0x3F);
        case 0xFF17: return apu->nr22;
        case 0xFF18: return 0xFF;
        case 0xFF19: return (uint8_t)(apu->nr24 | 0xBF);
        case 0xFF1A: return (uint8_t)(apu->nr30 | 0x7F);
        case 0xFF1B: return 0xFF;
        case 0xFF1C: return (uint8_t)(apu->nr32 | 0x9F);
        case 0xFF1D: return 0xFF;
        case 0xFF1E: return (uint8_t)(apu->nr34 | 0xBF);
        case 0xFF20: return 0xFF;
        case 0xFF21: return apu->nr42;
        case 0xFF22: return apu->nr43;
        case 0xFF23: return (uint8_t)(apu->nr44 | 0xBF);
        case 0xFF24: return apu->nr50;
        case 0xFF25: return apu->nr51;
        case 0xFF26: {
            uint8_t status = apu->enabled ? 0x80 : 0x00;
            for (int i = 0; i < 4; i++) if (apu->ch[i].enabled) status |= (uint8_t)(1 << i);
            return (uint8_t)(status | 0x70);
        }
        default: return 0xFF;
    }
}

// pandocs' Audio_details.md "Obscure Behavior": writing NRx4 with a
// 0->1 length-enable transition, on a step the frame sequencer wouldn't
// otherwise clock length on, immediately clocks length once (as if that
// upcoming step already ran) rather than waiting for the real edge -
// found needed (alongside trigger_length_reload() above) by Blargg's
// dmg_sound 03-trigger.gb and 08-len ctr during power.gb, both of which
// use this exact edge to precisely probe the internal length countdown
// (NRx1 itself is write-only, so there's no other way to read it back).
static void extra_length_clock_on_enable(GBApu *apu, int ch, uint8_t old_nrx4, uint8_t new_nrx4) {
    if (next_seq_step_clocks_length(apu)) return;
    if ((old_nrx4 & 0x40) || !(new_nrx4 & 0x40)) return;
    GBApuChannel *c = &apu->ch[ch];
    if (c->length_timer > 0) {
        c->length_timer--;
        if (c->length_timer == 0 && !(new_nrx4 & 0x80)) c->enabled = 0;
    }
}

void gb_apu_write(GBApu *apu, uint16_t addr, uint8_t val) {
    // Wave RAM is always accessible (pandocs' NR52 description: "does
    // not affect Wave RAM... which can always be read/written"). This
    // phase doesn't model the DMG's exact "only accessible on the same
    // cycle CH3 itself reads" mid-playback lock - see
    // docs/GAMEBOY_ROADMAP.md's Phase 5 status.
    if (addr >= 0xFF30 && addr <= 0xFF3F) {
        apu->wave_ram[addr - 0xFF30] = val;
        return;
    }

    if (addr == 0xFF26) {
        int was_enabled = apu->enabled;
        apu->enabled = (val & 0x80) != 0;
        if (was_enabled && !apu->enabled) {
            // Powering off clears every register (pandocs' NR52
            // description), NRx1 included - confirmed against Blargg's
            // dmg_sound 01-registers.gb test 5, which fills every
            // register (including NR11/21/31/41) with $FF before
            // powering off, then expects a full clear: NRx1's length
            // *data* bits are write-only and read back forced-high
            // regardless of this byte's value (see gb_apu_read()), but
            // NR11/NR21's duty bits (6-7) are real, readable bits that
            // must clear too. ch[i].length_timer (the internal countdown,
            // a separate struct field entirely) is deliberately left
            // untouched here - confirmed against dmg_sound 08-len ctr
            // during power.gb ("On DMG, [length counters] are
            // unaffected, and not clocked" while off - see
            // gb_apu_step()'s comment for the "not clocked" half).
            apu->nr10 = apu->nr12 = apu->nr13 = apu->nr14 = 0;
            apu->nr11 = apu->nr21 = apu->nr31 = apu->nr41 = 0;
            apu->nr22 = apu->nr23 = apu->nr24 = 0;
            apu->nr30 = apu->nr32 = apu->nr33 = apu->nr34 = 0;
            apu->nr42 = apu->nr43 = apu->nr44 = 0;
            apu->nr50 = apu->nr51 = 0;
            for (int i = 0; i < 4; i++) apu->ch[i].enabled = 0;
        }
        return;
    }

    // While powered off, every register write is ignored - EXCEPT NRx1's
    // length-counter *reload*, which pandocs' [^dmg_apu_off] footnote
    // ("and the length timers... on monochrome models") says still
    // reaches the internal counter on DMG, bypassing the register bank
    // this guard otherwise protects. Confirmed needed (not just a
    // footnote curiosity) by Blargg's dmg_sound 08-len ctr during
    // power.gb: fill_apu_regs's own loop writes every register
    // including NR52 last, leaving the APU off by the time the test's
    // "Load length counters" NR41/NR31/NR11/NR21 writes run - if those
    // were dropped like every other write, the counters would never get
    // their real starting values at all. The register's own *readable*
    // bits (NR11/NR21's duty, bits 6-7) do NOT change while off, only
    // ch[?].length_timer - confirmed separately against 01-registers.gb
    // test 6 (fills every register with $FF while off, then expects a
    // fully unaffected read-back afterward).
    if (!apu->enabled) {
        switch (addr) {
            case 0xFF11: apu->ch[0].length_timer = 64 - (val & 0x3F); break;
            case 0xFF16: apu->ch[1].length_timer = 64 - (val & 0x3F); break;
            case 0xFF1B: apu->ch[2].length_timer = 256 - val; break;
            case 0xFF20: apu->ch[3].length_timer = 64 - (val & 0x3F); break;
            default: break;
        }
        return;
    }

    switch (addr) {
        case 0xFF10: apu->nr10 = val; break;
        case 0xFF11: apu->nr11 = val; apu->ch[0].length_timer = 64 - (val & 0x3F); break;
        case 0xFF12: apu->nr12 = val; if (!dac_enabled(apu, 0)) apu->ch[0].enabled = 0; break;
        case 0xFF13: apu->nr13 = val; break;
        case 0xFF14: {
            uint8_t old = apu->nr14;
            apu->nr14 = val;
            extra_length_clock_on_enable(apu, 0, old, val);
            if (val & 0x80) trigger_ch1(apu);
            break;
        }

        case 0xFF16: apu->nr21 = val; apu->ch[1].length_timer = 64 - (val & 0x3F); break;
        case 0xFF17: apu->nr22 = val; if (!dac_enabled(apu, 1)) apu->ch[1].enabled = 0; break;
        case 0xFF18: apu->nr23 = val; break;
        case 0xFF19: {
            uint8_t old = apu->nr24;
            apu->nr24 = val;
            extra_length_clock_on_enable(apu, 1, old, val);
            if (val & 0x80) trigger_ch2(apu);
            break;
        }

        case 0xFF1A: apu->nr30 = val; if (!dac_enabled(apu, 2)) apu->ch[2].enabled = 0; break;
        case 0xFF1B: apu->nr31 = val; apu->ch[2].length_timer = 256 - val; break;
        case 0xFF1C: apu->nr32 = val; break;
        case 0xFF1D: apu->nr33 = val; break;
        case 0xFF1E: {
            uint8_t old = apu->nr34;
            apu->nr34 = val;
            extra_length_clock_on_enable(apu, 2, old, val);
            if (val & 0x80) trigger_ch3(apu);
            break;
        }

        case 0xFF20: apu->nr41 = val; apu->ch[3].length_timer = 64 - (val & 0x3F); break;
        case 0xFF21: apu->nr42 = val; if (!dac_enabled(apu, 3)) apu->ch[3].enabled = 0; break;
        case 0xFF22: apu->nr43 = val; break;
        case 0xFF23: {
            uint8_t old = apu->nr44;
            apu->nr44 = val;
            extra_length_clock_on_enable(apu, 3, old, val);
            if (val & 0x80) trigger_ch4(apu);
            break;
        }

        case 0xFF24: apu->nr50 = val; break;
        case 0xFF25: apu->nr51 = val; break;
        default: break;
    }
}
