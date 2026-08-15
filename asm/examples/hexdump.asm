; hexdump.asm - classic memory-dump utility: print a buffer as hex bytes
; with an ASCII sidebar (non-printable bytes shown as '.'). A natural
; companion to print16.asm's decimal conversion - this one converts a byte
; to hex instead, which only needs a nibble split and a lookup table
; rather than division.
;
; Self-checks hex_byte on four bytes (0x00, 0x0F, 0xA5, 0xFF - an all-zero
; byte, a zero high nibble, mixed digit/letter nibbles, and the max byte)
; before dumping a real 16-byte buffer for visual confirmation.

BDOS:   equ 5

        org 100h

start:
        ; --- Check 1: 0x00 ---
        ld a, 000h
        ld de, hexbuf
        call hex_byte
        ld a, (hexbuf)
        cp '0'
        jp nz, hexfail1
        ld a, (hexbuf+1)
        cp '0'
        jp nz, hexfail1
        ld de, hexok1
        call print
        jp hexcheck2
hexfail1: ld de, hexbad1
        call print

hexcheck2:
        ; --- Check 2: 0x0F (zero high nibble) ---
        ld a, 00Fh
        ld de, hexbuf
        call hex_byte
        ld a, (hexbuf)
        cp '0'
        jp nz, hexfail2
        ld a, (hexbuf+1)
        cp 'F'
        jp nz, hexfail2
        ld de, hexok2
        call print
        jp hexcheck3
hexfail2: ld de, hexbad2
        call print

hexcheck3:
        ; --- Check 3: 0xA5 (mixed letter/digit nibbles) ---
        ld a, 0A5h
        ld de, hexbuf
        call hex_byte
        ld a, (hexbuf)
        cp 'A'
        jp nz, hexfail3
        ld a, (hexbuf+1)
        cp '5'
        jp nz, hexfail3
        ld de, hexok3
        call print
        jp hexcheck4
hexfail3: ld de, hexbad3
        call print

hexcheck4:
        ; --- Check 4: 0xFF (max byte) ---
        ld a, 0FFh
        ld de, hexbuf
        call hex_byte
        ld a, (hexbuf)
        cp 'F'
        jp nz, hexfail4
        ld a, (hexbuf+1)
        cp 'F'
        jp nz, hexfail4
        ld de, hexok4
        call print
        jp demo
hexfail4: ld de, hexbad4
        call print

demo:
        ld de, demomsg
        call print

        ld hl, data
        call dump_row
        call dump_row

        jp 0

; dump_row: print 8 bytes at (HL) as hex (space-separated) followed by
; their ASCII sidebar (bytes outside printable ASCII shown as '.'), then
; CRLF. Advances HL past the 8 bytes. Destroys A, BC, DE.
dump_row:
        push hl                  ; keep the row start for the ASCII pass below
        ld b, 8
dump_hex_loop:
        ld a, (hl)
        ld de, hexbuf
        call hex_byte
        ld a, (hexbuf)
        call putchar
        ld a, (hexbuf+1)
        call putchar
        ld a, ' '
        call putchar
        inc hl
        djnz dump_hex_loop

        pop hl
        ld b, 8
dump_ascii_loop:
        ld a, (hl)
        cp ' '
        jr c, dump_nonprint
        cp 07Fh
        jr nc, dump_nonprint
        jr dump_putascii
dump_nonprint:
        ld a, '.'
dump_putascii:
        call putchar
        inc hl
        djnz dump_ascii_loop

        ld a, 13
        call putchar
        ld a, 10
        call putchar
        ret

; hex_byte: convert the byte in A to two ASCII hex digits at (DE).
; Destroys A. DE is advanced past the two digits written.
hex_byte:
        push af
        srl a
        srl a
        srl a
        srl a                     ; A = high nibble (0000hhhh)
        call hex_nibble
        ld (de), a
        inc de
        pop af
        and 00Fh
        call hex_nibble
        ld (de), a
        inc de
        ret

; hex_nibble: A (0-15) -> A = ASCII hex digit.
hex_nibble:
        cp 10
        jr c, hex_nibble_digit
        add a, 'A'-10
        ret
hex_nibble_digit:
        add a, '0'
        ret

; putchar: print the single character in A (BDOS function 2). Preserves
; every register.
putchar:
        push af
        push bc
        push de
        push hl
        ld e, a
        ld c, 2
        call BDOS
        pop hl
        pop de
        pop bc
        pop af
        ret

; DE = message pointer, BDOS print string + trailing CRLF, then return.
print:  ld c, 9
        call BDOS
        ld de, crlf
        ld c, 9
        call BDOS
        ret

hexbuf:   ds 2
crlf:     db 13, 10, '$'
demomsg:  db 13, 10, 'Demo:$'

; 16 bytes: a printable run followed by a mix of control bytes and 0xFF,
; so the demo dump shows both a clean ASCII sidebar and '.' substitution.
data:     db 'Hello, Z80!', 00Dh, 00Ah, 000h, 001h, 0FFh

hexok1:   db 'OK 1 hex_byte(00h)$'
hexbad1:  db 'FAIL 1 hex_byte(00h)$'
hexok2:   db 'OK 2 hex_byte(0Fh)$'
hexbad2:  db 'FAIL 2 hex_byte(0Fh)$'
hexok3:   db 'OK 3 hex_byte(A5h)$'
hexbad3:  db 'FAIL 3 hex_byte(A5h)$'
hexok4:   db 'OK 4 hex_byte(FFh)$'
hexbad4:  db 'FAIL 4 hex_byte(FFh)$'
