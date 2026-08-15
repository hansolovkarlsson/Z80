; A small "real" CP/M transient program exercising the BDOS file I/O in
; cpm.c (see cpm/docs/CPM_REFERENCE.md) - not a pass/fail regression check
; like asm/examples/file_test.asm, but something that actually does
; something visible: writes a short greeting to GREETING.TXT, lists the
; (mapped host) directory the way CP/M's own DIR.COM would, then reads
; the file back and TYPEs it to the console like CP/M's own TYPE.COM.
;
; Run it directly - no piped input needed:
;   bin/z80asm asm/test/filedemo.asm -o filedemo.com
;   bin/z80 filedemo.com

BDOS:   equ 5

        org 100h

start:
        ld de, banner
        ld c, 9
        call BDOS

        ; --- Build one 128-byte record: the message, ^Z-padded. CP/M text
        ; files conventionally end with ^Z (1Ah) so a reader knows where
        ; real content stops within the last (partial) record. ---
        ld hl, dmabuf
        ld b, 128
        ld a, 1Ah
fillpad:
        ld (hl), a
        inc hl
        djnz fillpad

        ld hl, message
        ld de, dmabuf
        ld b, msglen
copymsg:
        ld a, (hl)
        ld (de), a
        inc hl
        inc de
        djnz copymsg

        ; --- Write it: F_MAKE, F_WRITE one record, F_CLOSE ---
        ld de, fcb
        ld c, 22                 ; F_MAKE
        call BDOS
        cp 0FFh
        jp z, err_make

        ld de, dmabuf
        ld c, 26                 ; F_DMAOFF
        call BDOS

        ld de, fcb
        ld c, 21                 ; F_WRITE
        call BDOS
        or a
        jp nz, err_write

        ld de, fcb
        ld c, 16                 ; F_CLOSE
        call BDOS

        ; --- List the directory (like DIR *.*) ---
        ld de, hdr_dir
        ld c, 9
        call BDOS

        ld de, dirbuf
        ld c, 26                 ; F_DMAOFF -> dedicated dir-entry buffer
        call BDOS

        ld de, searchfcb
        ld c, 17                 ; F_SFIRST
        call BDOS
        jp dircheck
dirnext:
        ld c, 18                 ; F_SNEXT
        call BDOS
dircheck:
        cp 0FFh
        jp z, dirdone
        call print_dirent
        jp dirnext
dirdone:

        ; --- Read it back and TYPE it to the console ---
        ld de, hdr_type
        ld c, 9
        call BDOS

        ld de, fcb2
        ld c, 15                 ; F_OPEN
        call BDOS
        cp 0FFh
        jp z, err_open

        ld de, dmabuf2
        ld c, 26                 ; F_DMAOFF
        call BDOS

typeloop:
        ld de, fcb2
        ld c, 20                 ; F_READ
        call BDOS
        or a
        jp nz, typedone          ; EOF (or error) - stop

        ld hl, dmabuf2
        ld b, 128
typechar:
        ld a, (hl)
        cp 1Ah                   ; ^Z marks end of real content
        jp z, typedone
        push bc
        push hl
        ld e, a
        ld c, 2
        call BDOS
        pop hl
        pop bc
        inc hl
        djnz typechar
        jp typeloop
typedone:
        ld de, fcb2
        ld c, 16                 ; F_CLOSE
        call BDOS

        jp 0

err_make:
        ld de, m_makefail
        call print9
        jp 0
err_write:
        ld de, m_writefail
        call print9
        jp 0
err_open:
        ld de, m_openfail
        call print9
        jp 0

print9: ld c, 9
        call BDOS
        ret

; Prints one 11-byte (8 name + 3 type) directory-entry image at dirbuf+1
; as "NAME.TYP" + CRLF. Trims trailing spaces from the name field so
; "GREETING.TXT" doesn't come out "GREETING.TXT   " for a shorter name.
print_dirent:
        push af
        push bc
        push de
        push hl
        ld hl, dirbuf + 1
        ld b, 8
namechar:
        ld a, (hl)
        cp ' '
        jp z, nameskip
        push bc
        push hl
        ld e, a
        ld c, 2
        call BDOS
        pop hl
        pop bc
nameskip:
        inc hl
        djnz namechar

        ld e, '.'
        ld c, 2
        call BDOS

        ld b, 3
typechar2:
        ld a, (hl)
        push bc
        push hl
        ld e, a
        ld c, 2
        call BDOS
        pop hl
        pop bc
        inc hl
        djnz typechar2

        ld de, crlf
        ld c, 9
        call BDOS

        pop hl
        pop de
        pop bc
        pop af
        ret

banner:  db 'CP/M File I/O Demo', 13, 10, '$'
hdr_dir: db 13, 10, 'Files in cpm_disk/:', 13, 10, '$'
hdr_type: db 13, 10, '--- GREETING.TXT ---', 13, 10, '$'
crlf:    db 13, 10, '$'

m_makefail:  db 'F_MAKE failed$'
m_writefail: db 'F_WRITE failed$'
m_openfail:  db 'F_OPEN failed$'

; Must fit in one 128-byte record (dmabuf below) including the trailing
; ^Z, since this demo only calls F_WRITE once - a real multi-record file
; would need a loop writing 128 bytes at a time until fully flushed.
message: db 'Hello from a real CP/M program!', 13, 10
         db 'Written via F_MAKE/F_WRITE,', 13, 10
         db 'read back via F_OPEN/F_READ.', 13, 10, 26
msglen:  equ $ - message

fcb:    db 0, 'GREETING', 'TXT', 0, 0, 0, 0
        ds 16
        db 0, 0, 0, 0

fcb2:   db 0, 'GREETING', 'TXT', 0, 0, 0, 0
        ds 16
        db 0, 0, 0, 0

searchfcb: db 0, '????????', '???', 0, 0, 0, 0
        ds 16
        db 0, 0, 0, 0

dmabuf:  ds 128
dmabuf2: ds 128
dirbuf:  ds 128
