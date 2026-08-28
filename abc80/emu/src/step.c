// abc80/emu/src/step.c - the shared per-instruction step, extracted out of
// main.c's --interactive loop (Milestone 11) so bin/abc80 and the future
// bin/abc80-gtk don't each carry their own copy of this carefully-derived
// logic. See step.h's own comment for the full rationale, and
// abc80/docs/ABC80_ROADMAP.md's Milestone 11 section for the extraction
// itself - this file only moved the code, it didn't change it.

#include <stdbool.h>

#include "step.h"

#include "keyboard.h"

int abc80_step(Z80 *cpu, uint8_t *ram, Abc80SoundLog *sound_log,
               uint64_t *total_cycles, uint64_t *next_pio_interrupt_at) {
    // Real hardware address-decodes PIO Port A at several aliased port
    // numbers (see abc80_sync_pio_port_a()'s own comment) - kept in sync
    // here, once per instruction, so callers of abc80_step() don't need
    // to remember to call it themselves.
    abc80_sync_pio_port_a(cpu);

    uint16_t pc_before = cpu->pc;

    // Whether the *upcoming* z80_execute() call will actually fetch and
    // run the opcode at pc_before, or instead divert into interrupt-
    // acceptance and leave pc_before's real instruction un-executed until
    // some later call reaches this same PC again - mirrors z80_execute()'s
    // own acceptance check (z80.c) exactly, so it has to be evaluated
    // *before* that call, from the same CPU state. Needed once real
    // periodic interrupts existed: every `ram[pc_before]`-based prediction
    // below silently assumed z80_execute() always executes that exact
    // instruction, which stopped being true the moment interrupts could
    // intercept a step instead - caught by this project's own regression
    // testing, not hypothetical: typing `10 PRINT 1+1` after enabling the
    // interrupt dropped the "N" and produced a real `ERR 11`, from a
    // spurious abc80_keyboard_consumed() firing on a step that predicted
    // PC==0x0316 but actually served an interrupt that instruction
    // boundary instead, releasing the next host keystroke too early.
    bool interrupt_will_intercept_this_step =
        cpu->nmi_pending || (cpu->int_pending && cpu->iff1 && !cpu->ei_delay);

    // Edge-triggered strobe consumption, at the *specific* address where
    // this ROM actually finishes reading a key - not the first instruction
    // that merely detects the strobe is set. See keyboard.h's own comment
    // for why: this ROM's keyboard read is a debounce loop requiring the
    // strobe to stay asserted across several consecutive polls, converging
    // on 0x0316 (`IN A,(38h); AND 7Fh; RES 7,(HL); ...`) once the debounce
    // settles. Clearing there instead of on first detection lets that
    // debounce actually complete rather than being cut off after a single
    // poll; gated by interrupt_will_intercept_this_step above so an
    // intercepted step (pc_before==0x0316 predicted, but not actually
    // executed this call) doesn't fire early.
    bool about_to_consume_key =
        pc_before == 0x0316 && !interrupt_will_intercept_this_step;

    // OUT (n),A (0xD3) targeting the SN76477 sound register alias, or
    // BASIC's own compiled OUT port,value statement's ED-prefixed
    // register-indirect form (port from BC, value from whichever of
    // B/C/D/E/H/L/A the instruction names) - see sound.c's own top comment
    // for the port-0x06 bit layout and why both real opcode forms matter
    // (traced from actual BASIC-compiled code, not assumed). A doesn't
    // change across OUT, so it's still readable after z80_execute() runs.
    // Same interrupt-interception hazard as about_to_consume_key above
    // applies here too.
    bool about_to_write_sound = false;
    uint8_t sound_out_value = 0;
    if (interrupt_will_intercept_this_step) {
        // handled: leave about_to_write_sound false
    } else if (ram[pc_before] == 0xD3 && (ram[(uint16_t)(pc_before + 1)] & 0x17) == 0x06) {
        about_to_write_sound = true;
        sound_out_value = cpu->a;
    } else if (ram[pc_before] == 0xED && (ram[(uint16_t)(pc_before + 1)] & 0xC7) == 0x41 &&
               (cpu->c & 0x17) == 0x06) {
        about_to_write_sound = true;
        switch ((ram[(uint16_t)(pc_before + 1)] >> 3) & 0x07) {
            case 0: sound_out_value = cpu->b; break;
            case 1: sound_out_value = cpu->c; break;
            case 2: sound_out_value = cpu->d; break;
            case 3: sound_out_value = cpu->e; break;
            case 4: sound_out_value = cpu->h; break;
            case 5: sound_out_value = cpu->l; break;
            case 6: sound_out_value = 0; break; // undocumented OUT (C),0
            case 7: sound_out_value = cpu->a; break;
        }
    }

    // Milestone 6's disk bypass used to sit here, diverting the two real
    // block-I/O entry points (0x6068/0x60A1) into C instead of executing
    // them. Milestone 12 retired it: the ABC-bus card is a real device
    // model now (abcbus/disk.c, reached through this target's I/O hooks in
    // abcbus.c), so the DOS ROM's own protocol code runs for real and this
    // loop has nothing disk-specific left to predict.
    int cycles = z80_execute(cpu, ram);
    if (about_to_consume_key) {
        abc80_keyboard_consumed();
    }
    if (about_to_write_sound) {
        abc80_sound_write(sound_log, *total_cycles, sound_out_value);
    }

    if (cycles < 0) {
        return cycles;
    }
    *total_cycles += (uint64_t)cycles;

    // Real hardware's video-scanline-driven PIO interrupt runs
    // unconditionally from power-on, regardless of whether the ROM has
    // enabled interrupts yet - z80_request_int() is level-held
    // (cpu->int_pending stays set until actually serviced), so requesting
    // it early/often is harmless and correctly mirrors real hardware: the
    // CPU simply won't service it until its own IFF1 allows (the ROM's
    // `EI` at 0x00C5). A `while` (not `if`) keeps the schedule from
    // drifting even in the impossible case of a single instruction taking
    // longer than one period.
    while (*total_cycles >= *next_pio_interrupt_at) {
        z80_request_int(cpu, ABC80_PIO_INTERRUPT_VECTOR);
        *next_pio_interrupt_at += ABC80_PIO_INTERRUPT_PERIOD_TSTATES;
    }

    return cycles;
}
