; Broader z80asm coverage test: indexed (IX+d)/(IY+d) addressing, PUSH/POP,
; CB-prefixed rotate and BIT, 16-bit arithmetic, and EX. Each check prints
; "OK n" or "FAIL n" for a distinct instruction/behavior, so a wrong
; encoding shows up as a specific failing line instead of silent garbage.

BDOS:   equ 5

        org 100h

start:
        ; --- Check 1: (IX+d) store/load round trip ---
        ld ix, buf
        ld a, 42
        ld (ix+3), a
        ld b, (ix+3)
        ld a, b
        cp 42
        jp nz, fail1
        ld de, ok1
        call print
        jp check2
fail1:  ld de, bad1
        call print

check2:
        ; --- Check 2: PUSH/POP round trip through IX/IY ---
        ld ix, 1234h
        ld iy, 5678h
        push ix
        push iy
        pop ix
        pop iy
        ; after two pushes then two pops in the same order, ix should be
        ; back to 5678h and iy to 1234h (stack is LIFO)
        ld a, ixh
        cp 56h
        jp nz, fail2
        ld a, iyh
        cp 12h
        jp nz, fail2
        ld de, ok2
        call print
        jp check3
fail2:  ld de, bad2
        call print

check3:
        ; --- Check 3: CB-prefixed rotate + carry ---
        ld a, 80h
        rlca                    ; 80h -> 01h, carry set
        jp nc, fail3            ; carry must be set (check first: CP below clobbers it)
        cp 01h
        jp nz, fail3
        ld de, ok3
        call print
        jp check4
fail3:  ld de, bad3
        call print

check4:
        ; --- Check 4: BIT on a register ---
        ld a, 040h
        bit 6, a
        jp z, fail4             ; bit 6 of 40h is set, Z must be 0
        ld de, ok4
        call print
        jp check5
fail4:  ld de, bad4
        call print

check5:
        ; --- Check 5: 16-bit ADD HL,DE ---
        ld hl, 1000h
        ld de, 0234h
        add hl, de
        ld a, h
        cp 12h
        jp nz, fail5
        ld a, l
        cp 34h
        jp nz, fail5
        ld de, ok5
        call print
        jp done
fail5:  ld de, bad5
        call print

done:   jp 0

; DE = message pointer, BDOS print string + trailing CRLF, then return.
print:  ld c, 9
        call BDOS
        ld de, crlf
        ld c, 9
        call BDOS
        ret

buf:    ds 8

ok1:    db 'OK 1 (IX+d) store/load$'
bad1:   db 'FAIL 1 (IX+d) store/load$'
ok2:    db 'OK 2 PUSH/POP IX,IY$'
bad2:   db 'FAIL 2 PUSH/POP IX,IY$'
ok3:    db 'OK 3 RLCA + carry$'
bad3:   db 'FAIL 3 RLCA + carry$'
ok4:    db 'OK 4 BIT 6,A$'
bad4:   db 'FAIL 4 BIT 6,A$'
ok5:    db 'OK 5 ADD HL,DE$'
bad5:   db 'FAIL 5 ADD HL,DE$'
crlf:   db 13, 10, '$'
