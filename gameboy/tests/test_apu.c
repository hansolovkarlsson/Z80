#include <stdio.h>
#include <string.h>
#include "../src/apu.h"
#include "../src/cpu.h"

// apu.c's gb_apu_step() calls gb_read_byte() (for the frame sequencer's
// DIV read) - unused by these tests (which only exercise gb_apu_write(),
// pure GBApu-internal state), but still needed to satisfy the linker
// without pulling in the full mmu.c/cpu.c this file has no other use for.
uint8_t gb_read_byte(GBCpu *cpu, uint16_t addr) {
    (void)cpu;
    (void)addr;
    return 0;
}

// Direct unit tests for apu.c's "zombie mode" volume-nudge behavior -
// grounded against pandocs' Audio_details.md ("Obscure Behavior"
// section): writing NRx2 while a channel is already playing, with the
// envelope in increase mode and a period of zero, increments the
// live volume by 1 (wrapping mod 16) without needing a retrigger -
// the one form of "zombie mode" pandocs documents as consistent across
// every real hardware unit tested, including DMG (the general
// algorithm is explicitly "crazy"/unreliable on DMG, so only this
// narrow case is implemented - see apply_zombie_mode_increment()'s own
// comment in apu.c). Found necessary by a real ROM, Droneboy
// (gameboy/test_roms/droneboy/droneboy.gb), whose own volume-fader code
// relies on exactly this technique - see its README.md for the story.
//
// gb_apu_write()/gb_apu_reset() need no GBCpu at all (triggers and
// register writes are pure GBApu-internal state), unlike gb_apu_step()
// (which reads DIV off a real GBCpu for the frame sequencer) - so
// these tests, like test_cart.c's, need no mmu/cpu stub whatsoever.

static int failures = 0;

static void check(const char *name, int condition) {
    if (condition) {
        printf("OK   %s\n", name);
    } else {
        printf("FAIL %s\n", name);
        failures++;
    }
}

int main(void) {
    GBApu apu;
    gb_apu_reset(&apu, 44100, NULL, 0);

    // Trigger CH1 with initial volume 5 (NR12 = 0x58: volume=5,
    // increase=1, period=0), DAC enabled since the top 5 bits are nonzero.
    gb_apu_write(&apu, 0xFF12, 0x58);
    gb_apu_write(&apu, 0xFF14, 0x80); // trigger
    check("CH1 trigger: channel enabled", apu.ch[0].enabled);
    check("CH1 trigger: volume set from NR12's initial-volume field", apu.ch[0].volume == 5);

    // Repeated zombie-mode writes of the same 0x08 (volume field
    // irrelevant here - only the increase+period-zero low nibble
    // matters) should each nudge the live volume up by 1.
    gb_apu_write(&apu, 0xFF12, 0x08);
    check("CH1 zombie mode: one 0x08 write increments volume by 1", apu.ch[0].volume == 6);
    for (int i = 0; i < 4; i++) gb_apu_write(&apu, 0xFF12, 0x08);
    check("CH1 zombie mode: five total writes -> volume 5+5=10", apu.ch[0].volume == 10);

    // pandocs: "repeat 15 times to decrement the volume by 1" - a full
    // 4-bit wraparound (+1 fifteen times == -1 mod 16).
    for (int i = 0; i < 15; i++) gb_apu_write(&apu, 0xFF12, 0x08);
    check("CH1 zombie mode: 15 more writes wrap around to volume 9 (10-1)", apu.ch[0].volume == 9);

    // A write that doesn't match increase-mode+period-zero (here:
    // decrease mode, period 0 - low nibble 0x00) must NOT nudge the
    // live volume - only the exact documented-reliable pattern should.
    int before = apu.ch[0].volume;
    gb_apu_write(&apu, 0xFF12, 0x00);
    check("CH1 zombie mode: a non-matching NR12 write leaves volume unchanged",
          apu.ch[0].volume == before);

    // Same behavior on CH2 (NR22/NR24, 0xFF17/0xFF19) - the write
    // handler for NR22 needs its own apply_zombie_mode_increment() call,
    // not just CH1's.
    gb_apu_write(&apu, 0xFF17, 0x38); // volume=3, increase, period=0
    gb_apu_write(&apu, 0xFF19, 0x80); // trigger
    check("CH2 trigger: volume set from NR22", apu.ch[1].volume == 3);
    gb_apu_write(&apu, 0xFF17, 0x08);
    gb_apu_write(&apu, 0xFF17, 0x08);
    check("CH2 zombie mode: two 0x08 writes -> volume 3+2=5", apu.ch[1].volume == 5);

    // Same behavior on CH4/noise (NR42/NR44, 0xFF21/0xFF23).
    gb_apu_write(&apu, 0xFF21, 0x28); // volume=2, increase, period=0
    gb_apu_write(&apu, 0xFF23, 0x80); // trigger
    check("CH4 trigger: volume set from NR42", apu.ch[3].volume == 2);
    gb_apu_write(&apu, 0xFF21, 0x08);
    check("CH4 zombie mode: one 0x08 write -> volume 2+1=3", apu.ch[3].volume == 3);

    // A channel that was never triggered (never enabled) must not
    // respond to zombie-mode writes at all - real hardware only lets
    // you nudge a channel that's actually playing. CH2, not CH1: a
    // fresh gb_apu_reset() leaves ch[0] (CH1) already enabled, matching
    // the real DMG boot ROM's own startup chime (see gb_apu_reset()'s
    // own comment) - CH2 has no such special-casing, so it's a genuine
    // never-triggered channel to test against.
    GBApu apu2;
    gb_apu_reset(&apu2, 44100, NULL, 0);
    check("CH2 fresh reset: not enabled (sanity check for the next assertion)",
          !apu2.ch[1].enabled);
    gb_apu_write(&apu2, 0xFF17, 0x08); // no trigger first
    check("CH2 zombie mode: a never-triggered channel ignores the write",
          apu2.ch[1].volume == 0 && !apu2.ch[1].enabled);

    if (failures == 0) {
        printf("\nAll apu.c tests passed.\n");
        return 0;
    }
    printf("\n%d test(s) FAILED.\n", failures);
    return 1;
}
