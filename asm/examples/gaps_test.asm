; Exercises the emulator additions from cpm/docs/ROADMAP.md's "Known gaps"
; section: RETI/RETN, LD A,I/LD A,R/LD I,A/LD R,A, IM 0/1/2, and IN/OUT
; port I/O. None of these are covered by ZEXALL/ZEXDOC (it only checks
; documented CPU-internal instruction/flag behavior, not I/O or
; interrupt-control state), so this is the only regression coverage they
; have. Same "OK n"/"FAIL n" pattern as selftest.asm.

BDOS:   equ 5

        org 100h

start:
        ; --- Check 1: OUT (n),A / IN A,(n) round trip through a port ---
        ld a, 0A5h
        out (42h), a
        ld a, 0
        in a, (42h)
        cp 0A5h
        jp nz, fail1
        ld de, ok1
        call print
        jp check2
fail1:  ld de, bad1
        call print

check2:
        ; --- Check 2: OUT (C),r / IN r,(C) round trip through a port ---
        ld bc, 0055h            ; C = port 55h
        ld a, 3Ch
        out (c), a
        ld b, 0                 ; clobber B, then read back via IN B,(C)
        ld bc, 0055h
        in b, (c)
        ld a, b
        cp 3Ch
        jp nz, fail2
        ld de, ok2
        call print
        jp check3
fail2:  ld de, bad2
        call print

check3:
        ; --- Check 3: LD I,A / LD A,I round trip, plus P/V = IFF2 ---
        ld a, 77h
        ld i, a
        di                       ; IFF2 = 0
        ld a, 0
        ld a, i
        cp 77h
        jp nz, fail3
        jp pe, fail3             ; P/V must be reset (IFF2=0) after DI
        ei                       ; IFF2 = 1
        ld a, i
        jp po, fail3             ; P/V must be set (IFF2=1) after EI
        ld de, ok3
        call print
        jp check4
fail3:  ld de, bad3
        call print

check4:
        ; --- Check 4: LD R,A / LD A,R round trip. R increments by 1 per
        ; instruction fetch (including LD R,A's and LD A,R's own fetch),
        ; so writing V then immediately reading back yields V+1.
        ld a, 55h
        ld r, a                  ; R := 55h (this instr's own bump is overwritten)
        ld a, r                  ; R bumps to 56h first, then A := R
        cp 56h
        jp nz, fail4
        ld de, ok4
        call print
        jp check5
fail4:  ld de, bad4
        call print

check5:
        ; --- Check 5: IM 0/1/2 (canonical + undocumented duplicate
        ; encodings) execute without hitting the "unimplemented opcode"
        ; path. No architectural state is observable from user code, so
        ; this only proves they decode and don't corrupt execution flow.
        im 0
        im 1
        im 2
        db 0EDh, 04Eh            ; IM 0 (undocumented dup)
        db 0EDh, 066h            ; IM 0 (undocumented dup)
        db 0EDh, 06Eh            ; IM 0 (undocumented dup)
        db 0EDh, 076h            ; IM 1 (undocumented dup)
        db 0EDh, 07Eh            ; IM 2 (undocumented dup)
        im 0
        ld de, ok5
        call print
        jp check6

check6:
        ; --- Check 6: RETN / RETI pop PC and return control like RET.
        ; (Verifying RETN's IFF1:=IFF2 restore needs a live interrupt
        ; context, which the emulator doesn't deliver yet - see
        ; cpm/docs/ROADMAP.md's interrupt-delivery gap.)
        call sub_reti
        ld a, b
        cp 11h
        jp nz, fail6
        call sub_retn
        ld a, b
        cp 22h
        jp nz, fail6
        ld de, ok6
        call print
        jp done
fail6:  ld de, bad6
        call print

done:   jp 0

sub_reti:
        ld b, 11h
        reti
sub_retn:
        ld b, 22h
        retn

; DE = message pointer, BDOS print string + trailing CRLF, then return.
print:  ld c, 9
        call BDOS
        ld de, crlf
        ld c, 9
        call BDOS
        ret

ok1:    db 'OK 1 OUT (n),A / IN A,(n)$'
bad1:   db 'FAIL 1 OUT (n),A / IN A,(n)$'
ok2:    db 'OK 2 OUT (C),r / IN r,(C)$'
bad2:   db 'FAIL 2 OUT (C),r / IN r,(C)$'
ok3:    db 'OK 3 LD I,A / LD A,I + P/V=IFF2$'
bad3:   db 'FAIL 3 LD I,A / LD A,I + P/V=IFF2$'
ok4:    db 'OK 4 LD R,A / LD A,R$'
bad4:   db 'FAIL 4 LD R,A / LD A,R$'
ok5:    db 'OK 5 IM 0/1/2 (+ duplicates)$'
bad5:   db 'FAIL 5 IM 0/1/2 (+ duplicates)$'
ok6:    db 'OK 6 RETI / RETN$'
bad6:   db 'FAIL 6 RETI / RETN$'
crlf:   db 13, 10, '$'
