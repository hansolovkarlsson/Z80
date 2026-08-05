; Simple CP/M 2.2 Console I/O Test for Emulators
; Assemble using a tool like M80, Z80asm, or TCC/RunCPM environment

BDOS    EQU     0005h       ; BDOS entry point
WBOOT   EQU     0000h       ; Warm boot / exit

org 0100h                   ; Standard CP/M TPA start address

begin:
    ld      de, msg_hello   ; Print greeting string
    ld      c, 9            ; BDOS function 9: Output string
    call    BDOS

wait_key:
    ld      c, 11           ; BDOS function 11: Check console status
    call    BDOS
    or      a               ; Is a key pressed?
    jr      z, wait_key     ; If no, keep waiting

    ld      c, 1            ; BDOS function 1: Read console with echo
    call    BDOS
    ld      (char_store), a 

    ld      de, msg_done    ; Print newline/exit message
    ld      c, 9
    call    BDOS
    
    jp      WBOOT           ; Return control to CP/M CCP

msg_hello:
    db      'Type any key to test CP/M Console I/O:$'
msg_done:
    db      0Dh, 0Ah, 'Done. Returning to system.$'
char_store:
    db      0
