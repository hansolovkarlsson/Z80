; Minimal RGBDS "hello world" - proves the RGBDS toolchain (rgbasm,
; rgblink, rgbfix) round-trips correctly through this project's own
; emulator, not just that RGBDS itself works. Deliberately mirrors
; cpm/asm/examples/hello.asm's role on the Z80/CP-M side: the smallest
; possible real program, output-verified rather than just "didn't
; crash".
;
; Emits each character of MESSAGE over the serial port (SB/SC,
; $FF01/$FF02) using the same internal-clock transfer convention
; Blargg's own test ROMs use, which gameboy/src/mmu.c's serial hook
; already captures and gameboy/src/main.c prints to stdout - see
; make gameboy-rgbds-test.

SECTION "Header", ROM0[$100]
    jp EntryPoint
    ds $150 - @, 0 ; Space for the header RGBFIX fills in

SECTION "Main", ROM0[$150]
EntryPoint:
    ld hl, Message
.loop
    ld a, [hl+]
    and a
    jr z, .hang
    ld c, a
    call SerialSend
    jr .loop
.hang
    jr .hang

; Sends the byte in C over the serial port using an internal-clock
; transfer (SC = $81) - the same mechanism Blargg's cpu_instrs.gb/
; instr_timing.gb use, and the only serial behavior this emulator
; models at all (see mmu.h's own comment: no real serial hardware,
; just this one observation hook).
SerialSend:
    ld a, c
    ldh [$FF01], a ; SB
    ld a, $81
    ldh [$FF02], a ; SC: start transfer, internal clock
    ret

Message:
    db "HELLO GAMEBOY", 0
