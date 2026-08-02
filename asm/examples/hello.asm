; Small end-to-end test program for z80asm: prints a message, counts to 5
; with a DJNZ loop, prints the count as a digit, then warm-boots. Exercises
; labels, EQU, DB/DW/DS, 8/16-bit LD, INC/ADD, DJNZ, conditional and
; unconditional JP/CALL, and CP/M BDOS calls (functions 2 and 9).

BDOS:   equ 5

        org 100h

start:  ld de, msg
        ld c, 9
        call BDOS               ; print string

        ld b, 5
        ld hl, 0
count_loop:
        inc hl
        djnz count_loop         ; HL should end up holding 5

        ld a, l
        cp 5
        jp nz, fail

        add a, '0'
        ld e, a
        ld c, 2
        call BDOS               ; print '5'

        ld de, crlf
        ld c, 9
        call BDOS

        jp 0                    ; warm boot

fail:   ld de, failmsg
        ld c, 9
        call BDOS
        jp 0

msg:    db 'Hello from z80asm!$'
crlf:   db 13, 10, '$'
failmsg: db 'FAIL: loop counter wrong$'
