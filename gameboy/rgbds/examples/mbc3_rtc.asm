; MBC3 real-time-clock test ROM - drives the actual memory-mapped MBC3
; interface (bank-select writes at $4000-$5FFF, the RTC latch sequence
; at $6000-$7FFF, and the shared $A000-$BFFF read/write window) the way
; a real MBC3+RTC game would, rather than testing gameboy/src/cart.c's
; GBCart struct directly the way gameboy/tests/test_cart.c's synthetic
; checks already do - a genuinely different, real-hardware-shaped way
; of exercising the identical logic. See gameboy/docs/GAMEBOY_ROADMAP.md's
; Phase 6 status ("particularly ones exercising MBC3's RTC or deeper
; save-RAM behavior, neither meaningfully exercised by 2048-gb") - this
; closes that specific, previously-flagged gap.
;
; Also exercises plain banked cartridge RAM (banks 0 and 1) alongside
; the RTC registers, since real MBC3+RTC+RAM+BATTERY carts (cart type
; $10, what this ROM's header declares) share the same bank-select and
; $A000 window for both - proving they stay correctly isolated from
; each other, not just that RTC alone works.
;
; Emits a single deterministic line over serial (SB/SC, the same
; mechanism hello.asm/Blargg's own test ROMs use) - see
; gameboy/rgbds/README.md and make gameboy-rgbds-mbc3-test.
;
; NOTE ON SCOPE: gameboy/src/cart.c's own comment is explicit that the
; RTC registers don't yet advance with real elapsed (wall-clock or
; emulated) time - they're a correctly-latchable, correctly-isolated
; register bank, but a static one. This ROM tests exactly that scope:
; write-then-latch-then-read fidelity and live/latched isolation, not
; "does time actually pass" (which isn't implemented to test yet).

SECTION "Header", ROM0[$100]
    jp EntryPoint
    ds $150 - @, 0 ; Space for the header RGBFIX fills in

SECTION "Main", ROM0[$150]
EntryPoint:
    ; Enable RAM/RTC register access - real MBC3: writing $0A anywhere
    ; in $0000-$1FFF enables both (gameboy/src/cart.c's own comment:
    ; "also gates RTC register access").
    ld a, $0A
    ld [$0000], a

    ; --- Banked RAM isolation: two banks, two different sentinel bytes ---
    ld hl, RamLabel
    call PrintString
    ld a, $00 ; select RAM bank 0
    ld [$4000], a
    ld a, 'R'
    ld [$A000], a
    ld a, $01 ; select RAM bank 1
    ld [$4000], a
    ld a, 'r'
    ld [$A000], a
    ; read both back, in the same order written
    ld a, $00
    ld [$4000], a
    ld a, [$A000]
    ld c, a
    call SerialSend
    ld a, $01
    ld [$4000], a
    ld a, [$A000]
    ld c, a
    call SerialSend

    ; --- RTC round 1: write live S/M/H/DL/DH, then latch ---
    ld hl, Rtc1Label
    call PrintString
    ld a, 'A'
    call WriteRtcS
    ld a, 'B'
    call WriteRtcM
    ld a, 'C'
    call WriteRtcH
    ld a, 'D'
    call WriteRtcDL
    ld a, 'E'
    call WriteRtcDH
    call LatchRtc
    call ReadRtcAll ; expect A B C D E - the just-latched snapshot

    ; --- RTC round 2: overwrite live registers, but do NOT re-latch ---
    ld hl, Rtc2Label
    call PrintString
    ld a, 'a'
    call WriteRtcS
    ld a, 'b'
    call WriteRtcM
    ld a, 'c'
    call WriteRtcH
    ld a, 'd'
    call WriteRtcDL
    ld a, 'e'
    call WriteRtcDH
    call ReadRtcAll ; expect STILL A B C D E - the old latch, untouched

    ; --- RTC round 3: re-latch, now picking up the round-2 live values ---
    ld hl, Rtc3Label
    call PrintString
    call LatchRtc
    call ReadRtcAll ; expect a b c d e - the fresh snapshot

    ld hl, DoneLabel
    call PrintString
.hang
    jr .hang

; --- RTC register helpers - each selects its own register bank at
; $4000-$5FFF, then writes A into it via $A000, per gameboy/src/cart.c's
; own bank-to-register mapping (S=$08, M=$09, H=$0A, DL=$0B, DH=$0C).
WriteRtcS:
    ld b, a
    ld a, $08
    ld [$4000], a
    ld a, b
    ld [$A000], a
    ret
WriteRtcM:
    ld b, a
    ld a, $09
    ld [$4000], a
    ld a, b
    ld [$A000], a
    ret
WriteRtcH:
    ld b, a
    ld a, $0A
    ld [$4000], a
    ld a, b
    ld [$A000], a
    ret
WriteRtcDL:
    ld b, a
    ld a, $0B
    ld [$4000], a
    ld a, b
    ld [$A000], a
    ret
WriteRtcDH:
    ld b, a
    ld a, $0C
    ld [$4000], a
    ld a, b
    ld [$A000], a
    ret

; Writing $00 then $01 to $6000-$7FFF snapshots the live RTC registers
; into the latched copy that $A000 actually exposes while a register
; bank of $08-$0C is selected.
LatchRtc:
    ld a, $00
    ld [$6000], a
    ld a, $01
    ld [$6000], a
    ret

; Reads the latched S,M,H,DL,DH (bank $08-$0C) in order and sends each
; raw byte straight over serial - every value this ROM ever writes is
; already a printable ASCII sentinel, so no hex-formatting is needed.
ReadRtcAll:
    ld a, $08
    ld [$4000], a
    ld a, [$A000]
    ld c, a
    call SerialSend
    ld a, $09
    ld [$4000], a
    ld a, [$A000]
    ld c, a
    call SerialSend
    ld a, $0A
    ld [$4000], a
    ld a, [$A000]
    ld c, a
    call SerialSend
    ld a, $0B
    ld [$4000], a
    ld a, [$A000]
    ld c, a
    call SerialSend
    ld a, $0C
    ld [$4000], a
    ld a, [$A000]
    ld c, a
    call SerialSend
    ret

; Prints a null-terminated string pointed to by HL over serial.
PrintString:
    ld a, [hl+]
    and a
    ret z
    ld c, a
    call SerialSend
    jr PrintString

SerialSend:
    ld a, c
    ldh [$FF01], a ; SB
    ld a, $81
    ldh [$FF02], a ; SC: start transfer, internal clock
    ret

RamLabel:  db "RAM:", 0
Rtc1Label: db " RTC1:", 0
Rtc2Label: db " RTC2(unlatched):", 0
Rtc3Label: db " RTC3(relatched):", 0
DoneLabel: db " DONE", 0
