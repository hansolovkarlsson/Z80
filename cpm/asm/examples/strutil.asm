; strutil.asm - in-place string utilities operating on '$'-terminated
; strings (CP/M's own string convention): reverse, uppercase, lowercase.
; Together with print16.asm (numbers) and hexdump.asm (raw bytes), this
; rounds out the three classic building blocks a text-processing utility
; needs.
;
; str_reverse avoids any pointer-crossing comparison (awkward on the Z80 -
; there's no single instruction for a 16-bit less-than test) by computing
; the swap count up front as length/2 and counting down, so the two
; pointers are guaranteed to meet without ever needing to check.
;
; Self-checks each routine against a known input/output pair before
; printing a demonstration.

BDOS:   equ 5

        org 100h

start:
        ; --- Check 1: str_reverse ---
        ld hl, revsrc
        call str_reverse
        ld hl, revsrc
        ld de, revexp
        call strcmp_dollar
        jp nz, fail1
        ld de, ok1
        call print
        jp check2
fail1:  ld de, bad1
        call print

check2:
        ; --- Check 2: str_upper ---
        ld hl, uppersrc
        call str_upper
        ld hl, uppersrc
        ld de, upperexp
        call strcmp_dollar
        jp nz, fail2
        ld de, ok2
        call print
        jp check3
fail2:  ld de, bad2
        call print

check3:
        ; --- Check 3: str_lower ---
        ld hl, lowersrc
        call str_lower
        ld hl, lowersrc
        ld de, lowerexp
        call strcmp_dollar
        jp nz, fail3
        ld de, ok3
        call print
        jp demo
fail3:  ld de, bad3
        call print

demo:
        ld de, demomsg
        call print

        ld de, demosrc
        call print
        ld hl, demosrc
        call str_reverse
        ld de, demosrc
        call print

        ld de, demomixed
        call print
        ld hl, demomixed
        call str_upper
        ld de, demomixed
        call print

        jp 0

; str_reverse: reverse the '$'-terminated string at (HL) in place.
; Destroys A, BC, DE, HL.
str_reverse:
        ld (strrev_left), hl
        ld c, 0
strrev_scan:
        ld a, (hl)
        cp '$'
        jr z, strrev_havelen
        inc hl
        inc c
        jr strrev_scan
strrev_havelen:
        ; HL = address of the '$' terminator, C = string length
        ld a, c
        or a
        jr z, strrev_done         ; empty string
        cp 1
        jr z, strrev_done         ; single character, nothing to swap

        dec hl                    ; HL = address of the last real character
        ld (strrev_right), hl

        srl a                     ; A = length / 2 = number of swaps needed
        ld (strrev_count), a

        ld hl, (strrev_left)
        ld de, (strrev_right)
strrev_swaploop:
        ld a, (hl)
        ld b, a
        ld a, (de)
        ld (hl), a
        ld a, b
        ld (de), a
        inc hl
        dec de
        ld a, (strrev_count)
        dec a
        ld (strrev_count), a
        jr nz, strrev_swaploop
strrev_done:
        ret

; str_upper: convert lowercase ASCII letters in the '$'-terminated string
; at (HL) to uppercase, in place. Destroys A, HL.
str_upper:
        ld a, (hl)
        cp '$'
        ret z
        cp 'a'
        jr c, str_upper_next
        cp 'z' + 1
        jr nc, str_upper_next
        sub 'a' - 'A'
        ld (hl), a
str_upper_next:
        inc hl
        jr str_upper

; str_lower: convert uppercase ASCII letters in the '$'-terminated string
; at (HL) to lowercase, in place. Destroys A, HL.
str_lower:
        ld a, (hl)
        cp '$'
        ret z
        cp 'A'
        jr c, str_lower_next
        cp 'Z' + 1
        jr nc, str_lower_next
        add a, 'a' - 'A'
        ld (hl), a
str_lower_next:
        inc hl
        jr str_lower

; strcmp_dollar: compare '$'-terminated strings at (HL) and (DE).
; Z set if equal, NZ if not.
strcmp_dollar:
cmp_loop:
        ld a, (de)
        ld b, a
        ld a, (hl)
        cp b
        jr nz, cmp_done
        cp '$'
        jr z, cmp_done
        inc hl
        inc de
        jr cmp_loop
cmp_done:
        ret

; DE = message pointer, BDOS print string + trailing CRLF, then return.
print:  ld c, 9
        call BDOS
        ld de, crlf
        ld c, 9
        call BDOS
        ret

crlf:      db 13, 10, '$'
demomsg:   db 13, 10, 'Demo:$'

strrev_left:  dw 0
strrev_right: dw 0
strrev_count: db 0

revsrc:    db 'HELLO$'
revexp:    db 'OLLEH$'
uppersrc:  db 'Hello, Z80!$'
upperexp:  db 'HELLO, Z80!$'
lowersrc:  db 'Hello, Z80!$'
lowerexp:  db 'hello, z80!$'

demosrc:   db 'Z80 EMULATOR$'
demomixed: db 'Cp/M Turns 50$'

ok1:    db 'OK 1 str_reverse$'
bad1:   db 'FAIL 1 str_reverse$'
ok2:    db 'OK 2 str_upper$'
bad2:   db 'FAIL 2 str_upper$'
ok3:    db 'OK 3 str_lower$'
bad3:   db 'FAIL 3 str_lower$'
