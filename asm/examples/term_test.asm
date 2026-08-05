; Exercises console_emit()'s legacy-terminal-protocol translation in
; emu/src/cpm.c: ADM-3A cursor addressing (ESC = row col) and VT52/H19
; cursor addressing (ESC Y row col), VT52 erase-to-end-of-line (ESC K),
; H19 erase-entire-line (ESC l), and confirming a real ANSI CSI sequence
; still passes through untouched. Not self-checking (unlike selftest.asm's
; OK-n/FAIL-n convention) - there's no CP/M-visible way for a program to
; read back its own translated console output, so tests/run_tests.sh has
; a dedicated check that greps the raw byte stream this program produces
; for the expected ANSI translation instead.

BDOS: equ 5

        org 100h

start:
        ; ADM-3A cursor addressing: row=1,col=1 -> real ANSI row2;col2
        ld a, 1Bh
        call out
        ld a, '='
        call out
        ld a, 21h        ; row+32 = 1+32
        call out
        ld a, 21h        ; col+32 = 1+32
        call out

        ; VT52 cursor addressing: row=0,col=0 -> real ANSI row1;col1
        ld a, 1Bh
        call out
        ld a, 'Y'
        call out
        ld a, 20h
        call out
        ld a, 20h
        call out

        ; VT52 erase-to-end-of-line
        ld a, 1Bh
        call out
        ld a, 'K'
        call out

        ; H19 erase-entire-line
        ld a, 1Bh
        call out
        ld a, 'l'
        call out

        ; A real ANSI CSI sequence must still pass through untouched
        ld a, 1Bh
        call out
        ld a, '['
        call out
        ld a, '1'
        call out
        ld a, 'm'
        call out

        jp 0

; A = char to print via BDOS console output (function 2)
out:    push af
        ld e, a
        ld c, 2
        call BDOS
        pop af
        ret
