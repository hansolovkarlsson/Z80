; Shared definitions for include_test.asm - demonstrates that a macro
; defined in an included file is usable back in the including file.

BDOS: equ 5

PRINTMSG MACRO msg
        ld de, &msg
        ld c, 9
        call BDOS
ENDM
