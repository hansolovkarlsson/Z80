; Tests MACRO/ENDM with &param substitution and LOCAL label renaming.
; PRINTX is invoked twice below specifically to prove LOCAL works: without
; per-expansion-unique renaming, the second invocation's `&loop:` label
; would collide with the first and the assembler would reject it. Note
; LOCAL names are referenced with & too, like parameters - matching
; zexall.z80's own "local lab" / "&lab:" convention.

BDOS: equ 5

; Prints a $-terminated message given its label.
PRINTMSG MACRO msg
        ld de, &msg
        ld c, 9
        call BDOS
ENDM

; Prints 'X' `count` times. Invoked twice below - if LOCAL didn't
; actually rename `loop` per-expansion, the second invocation's `&loop:`
; would collide with the first and the assembler would error out.
PRINTX MACRO count
        LOCAL loop
        ld b, &count
&loop:  ld e, 'X'
        ld c, 2
        call BDOS
        djnz &loop
ENDM

        org 100h

        PRINTMSG msg1
        PRINTX 3
        PRINTMSG msg2
        PRINTX 5
        PRINTMSG msg3

        jp 0

msg1:   db 'Macro test: $'
msg2:   db ' and $'
msg3:   db ' done', 13, 10, '$'
