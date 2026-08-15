; print16.asm - convert a 16-bit value to decimal and print it: a real,
; reusable building block missing from the example library so far. No BDOS
; function prints a number directly, and the Z80 has no hardware divide
; instruction, so turning a binary value into decimal digits needs an
; explicit division routine - this is the standard one.
;
; div16by8 is the textbook 16-bit/8-bit shift-subtract ("restoring
; division") routine. num_to_str repeatedly divides by 10, pushing each
; remainder onto the stack as it's produced (least-significant digit
; first), then pops them back off - which naturally hands them back in
; most-significant-first order, exactly the order they need to print in.
;
; Self-checks five conversions (0, 9, 10, 1234, 65535 - zero, a single
; digit, a digit next to an embedded zero, a middling value, and the max
; 16-bit value) against known-good strings before printing each one too,
; for visual confirmation alongside the OK-n/FAIL-n checks.

BDOS:   equ 5

        org 100h

start:
        ; --- Check 1: zero ---
        ld hl, 0
        ld de, buf
        call num_to_str
        ld hl, buf
        ld de, s0
        call strcmp_dollar
        jp nz, fail1
        ld de, ok1
        call print
        jp check2
fail1:  ld de, bad1
        call print

check2:
        ; --- Check 2: single digit ---
        ld hl, 9
        ld de, buf
        call num_to_str
        ld hl, buf
        ld de, s9
        call strcmp_dollar
        jp nz, fail2
        ld de, ok2
        call print
        jp check3
fail2:  ld de, bad2
        call print

check3:
        ; --- Check 3: embedded zero digit ---
        ld hl, 10
        ld de, buf
        call num_to_str
        ld hl, buf
        ld de, s10
        call strcmp_dollar
        jp nz, fail3
        ld de, ok3
        call print
        jp check4
fail3:  ld de, bad3
        call print

check4:
        ; --- Check 4: four digits ---
        ld hl, 1234
        ld de, buf
        call num_to_str
        ld hl, buf
        ld de, s1234
        call strcmp_dollar
        jp nz, fail4
        ld de, ok4
        call print
        jp check5
fail4:  ld de, bad4
        call print

check5:
        ; --- Check 5: max 16-bit value ---
        ld hl, 65535
        ld de, buf
        call num_to_str
        ld hl, buf
        ld de, s65535
        call strcmp_dollar
        jp nz, fail5
        ld de, ok5
        call print
        jp demo
fail5:  ld de, bad5
        call print

demo:
        ; Visual confirmation: print the same five values through the same
        ; routine that was just self-checked above.
        ld de, demomsg
        call print

        ld hl, 0
        call print_num
        ld hl, 9
        call print_num
        ld hl, 10
        call print_num
        ld hl, 1234
        call print_num
        ld hl, 65535
        call print_num

        jp 0

; print_num: convert HL to decimal and print it (CRLF-terminated).
print_num:
        push de
        ld de, buf
        call num_to_str
        ld de, buf
        call print
        pop de
        ret

; num_to_str: convert HL (0-65535) to a '$'-terminated decimal string at
; (DE). Destroys A, BC, HL.
num_to_str:
        ld a, h
        or l
        jr nz, ntos_nonzero
        ld a, '0'
        ld (de), a
        inc de
        jr ntos_terminate
ntos_nonzero:
        ld b, 0                 ; digit count
ntos_divloop:
        ld a, h
        or l
        jr z, ntos_popdigits
        push bc                 ; div16by8 uses B as its own loop counter,
        ld c, 10                ; so the running digit count has to survive
        call div16by8           ; the call - HL = HL/10, A = remainder
        pop bc
        push af                 ; stash the digit; the stack hands digits
        inc b                   ; back most-significant-first once popped
        jr ntos_divloop
ntos_popdigits:
        pop af
        add a, '0'
        ld (de), a
        inc de
        djnz ntos_popdigits
ntos_terminate:
        ld a, '$'
        ld (de), a
        ret

; div16by8: HL = HL / C, A = HL mod C. Standard 16-bit-by-8-bit
; shift-subtract division - the Z80 has no divide instruction, so every
; digit num_to_str produces goes through this.
div16by8:
        xor a
        ld b, 16
div_loop:
        add hl, hl               ; shift dividend/quotient left; old bit 15
        rla                      ; of HL -> carry -> shifted into remainder
        cp c
        jr c, div_skip
        sub c
        inc l                    ; record a quotient bit of 1 in the bit
div_skip:                        ; add hl,hl just shifted in as 0 (safe:
        djnz div_loop             ; l is always even right here)
        ret

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

buf:      ds 8
crlf:     db 13, 10, '$'
demomsg:  db 13, 10, 'Demo:$'

s0:       db '0$'
s9:       db '9$'
s10:      db '10$'
s1234:    db '1234$'
s65535:   db '65535$'

ok1:    db 'OK 1 num_to_str(0)$'
bad1:   db 'FAIL 1 num_to_str(0)$'
ok2:    db 'OK 2 num_to_str(9)$'
bad2:   db 'FAIL 2 num_to_str(9)$'
ok3:    db 'OK 3 num_to_str(10)$'
bad3:   db 'FAIL 3 num_to_str(10)$'
ok4:    db 'OK 4 num_to_str(1234)$'
bad4:   db 'FAIL 4 num_to_str(1234)$'
ok5:    db 'OK 5 num_to_str(65535)$'
bad5:   db 'FAIL 5 num_to_str(65535)$'
