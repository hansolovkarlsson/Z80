; Translated from cpm/resources/user_prompt.txt (8080 mnemonics: MVI/LXI/CALL/
; RET) into Z80 syntax (LD/CALL/RET) - our assembler is Z80-only, same
; reason the bare-metal CPUville TinyBASIC didn't work directly either.
; Logic is otherwise unchanged: BDOS function 9 (print string) for the
; prompt, function 10 (buffered console input) to read a line, matching
; what cpm/emu/src/cpm.c already implements and cpm/asm/examples/console_test.asm
; already regression-tests - this is a fresh, independent exercise of the
; same BDOS functions from a different (user-supplied) source.
;
;   bin/z80asm cpm/asm/test/user_prompt.asm -o user_prompt.com
;   bin/z80 user_prompt.com

        ORG     100H

BDOS    EQU     0005H
PSTRING EQU     9
RBUFFER EQU     10

START:
        ; Step 1: Print the prompt string to the console
        LD      C, PSTRING
        LD      DE, PROMPT
        CALL    BDOS

        ; Step 2: Read the user's input into the buffer
        LD      C, RBUFFER
        LD      DE, INBUF
        CALL    BDOS

        ; Step 3: Print a newline sequence for formatting
        LD      C, PSTRING
        LD      DE, NL
        CALL    BDOS

        ; Step 4: Terminate program and return to the CCP prompt
        RET

; -------------------------------------------------------------------------
; Data Segment
; -------------------------------------------------------------------------

PROMPT: DB      'Enter your name: $'
NL:     DB      0DH, 0AH, '$'

; CP/M buffered input (Function 10) data structure:
; Byte 1: Maximum number of characters allowed to be read (user defined)
; Byte 2: Actual number of characters typed by user (filled by CP/M)
; Byte 3+: The actual character bytes typed by the user
INBUF:  DB      50
        DB      0
        DS      50
