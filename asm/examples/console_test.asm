; Exercises the BDOS console input functions added to emu/src/cpm.c per
; docs/CPM_REFERENCE.md: C_STAT (11), C_READ (1), C_RAWIO poll-read
; (6, E=0FFh), and C_READSTR (10). Not interactive - meant to be driven
; with piped stdin, e.g.:
;
;   printf 'ABOK\r' | bin/z80 console_test.com
;
; (see tests/run_tests.sh, which drives it exactly this way). Same "OK
; n"/"FAIL n" convention as selftest.asm/gaps_test.asm.

BDOS:   equ 5

        org 100h

start:
        ; --- Check 1: C_STAT sees the piped input as already available ---
        ld c, 11
        call BDOS
        or a
        jp z, fail1
        ld de, ok1
        call print
        jp check2
fail1:  ld de, bad1
        call print

check2:
        ; --- Check 2: C_READ reads the first byte ('A'), echoing it itself ---
        ld c, 1
        call BDOS
        cp 'A'
        jp nz, fail2
        ld de, ok2
        call print
        jp check3
fail2:  ld de, bad2
        call print

check3:
        ; --- Check 3: C_RAWIO poll-read (E=0FFh) reads 'B' with no echo ---
        ld c, 6
        ld e, 0FFh
        call BDOS
        cp 'B'
        jp nz, fail3
        ld de, ok3
        call print
        jp check4
fail3:  ld de, bad3
        call print

check4:
        ; --- Check 4: C_READSTR reads "OK" up to the trailing CR ---
        ld de, buf
        ld c, 10
        call BDOS
        ld a, (buf+1)            ; actual character count written back
        cp 2
        jp nz, fail4
        ld a, (buf+2)
        cp 'O'
        jp nz, fail4
        ld a, (buf+3)
        cp 'K'
        jp nz, fail4
        ld de, ok4
        call print
        jp done
fail4:  ld de, bad4
        call print

done:   jp 0

; DE = message pointer, BDOS print string + trailing CRLF, then return.
print:  ld c, 9
        call BDOS
        ld de, crlf
        ld c, 9
        call BDOS
        ret

buf:    db 10, 0                 ; max length 10, actual count (filled in)
        ds 10

ok1:    db 'OK 1 C_STAT (11) sees piped input$'
bad1:   db 'FAIL 1 C_STAT (11)$'
ok2:    db 'OK 2 C_READ (1)$'
bad2:   db 'FAIL 2 C_READ (1)$'
ok3:    db 'OK 3 C_RAWIO poll-read (6, E=FFh)$'
bad3:   db 'FAIL 3 C_RAWIO poll-read (6, E=FFh)$'
ok4:    db 'OK 4 C_READSTR (10)$'
bad4:   db 'FAIL 4 C_READSTR (10)$'
crlf:   db 13, 10, '$'
