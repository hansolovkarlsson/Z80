; A small "real" CP/M file-copy utility (like PIP/COPY.COM), exercising
; genuine multi-record sequential I/O - the gap cpm/asm/test/filedemo.asm's
; comment flagged (it only ever writes one 128-byte record). Not a
; pass/fail regression check; meant to be run and read by hand.
;
; What it does:
;   1. Writes SOURCE.TXT as several 128-byte records, DMA'd straight out
;      of the resident message text in this program's own image (no
;      manual copy-into-a-buffer step, so there's no fixed-size buffer to
;      overflow regardless of how long the message gets).
;   2. Copies SOURCE.TXT to DEST.TXT the way a real copy utility has to:
;      read a record, write a record, repeat until F_READ reports EOF -
;      it never needs to know the file's size up front.
;   3. Prints both files' size in records (F_SIZE) to show they match,
;      lists the directory (F_SFIRST/F_SNEXT), then TYPEs DEST.TXT back to
;      prove the copy is byte-for-byte correct.
;
;   bin/z80asm cpm/asm/test/copydemo.asm -o copydemo.com
;   bin/z80 copydemo.com

BDOS:   equ 5

        org 100h

start:
        ld de, banner
        ld c, 9
        call BDOS

        ; --- Write SOURCE.TXT: DMA points straight at each 128-byte
        ; chunk of `message` in this program's own resident image, so no
        ; separate copy-into-a-buffer step (and nothing to overflow). ---
        ld de, srcfcb
        ld c, 22                 ; F_MAKE
        call BDOS
        cp 0FFh
        jp z, err_make

        ld hl, message
        ld b, numrecords
writeloop:
        push bc
        ex de, hl
        ld c, 26                 ; F_DMAOFF -> current record's own memory
        call BDOS
        ex de, hl
        ld de, srcfcb
        ld c, 21                 ; F_WRITE
        call BDOS
        or a
        jp nz, err_write
        pop bc
        ld de, 128
        add hl, de
        djnz writeloop

        ld de, srcfcb
        ld c, 16                 ; F_CLOSE
        call BDOS

        ; --- Copy SOURCE.TXT -> DEST.TXT, one record at a time, until EOF ---
        ld de, hdr_copy
        ld c, 9
        call BDOS

        ld de, srcfcb2            ; fresh FCB: F_MAKE above already used srcfcb
        ld c, 15                  ; F_OPEN (read side)
        call BDOS
        cp 0FFh
        jp z, err_open

        ld de, dstfcb
        ld c, 22                  ; F_MAKE (write side)
        call BDOS
        cp 0FFh
        jp z, err_make

        ld de, readbuf
        ld c, 26                  ; F_DMAOFF - shared by both read and write
        call BDOS

copyloop:
        ld de, srcfcb2
        ld c, 20                  ; F_READ
        call BDOS
        or a
        jp nz, copydone            ; EOF - source fully copied

        ld de, dstfcb
        ld c, 21                   ; F_WRITE
        call BDOS
        or a
        jp nz, err_write

        jp copyloop
copydone:
        ld de, srcfcb2
        ld c, 16                   ; F_CLOSE
        call BDOS
        ld de, dstfcb
        ld c, 16
        call BDOS

        ; --- Show both files' size in records ---
        ld de, hdr_sizes
        ld c, 9
        call BDOS

        ld de, srcfcb3
        ld c, 35                   ; F_SIZE
        call BDOS
        ld a, (srcfcb3 + 21h)      ; R0 (offset 21h/33) = size in records
        call print_digit
        ld de, sizemsg1
        ld c, 9
        call BDOS

        ld de, dstfcb2
        ld c, 35                   ; F_SIZE
        call BDOS
        ld a, (dstfcb2 + 21h)      ; R0 (offset 21h/33) = size in records
        call print_digit
        ld de, sizemsg2
        ld c, 9
        call BDOS

        ; --- Directory listing ---
        ld de, hdr_dir
        ld c, 9
        call BDOS

        ld de, dirbuf
        ld c, 26
        call BDOS

        ld de, searchfcb
        ld c, 17                   ; F_SFIRST
        call BDOS
        jp dircheck
dirnext:
        ld c, 18                   ; F_SNEXT
        call BDOS
dircheck:
        cp 0FFh
        jp z, dirdone
        call print_dirent
        jp dirnext
dirdone:

        ; --- TYPE DEST.TXT to prove the copy is correct ---
        ld de, hdr_type
        ld c, 9
        call BDOS

        ld de, dstfcb3
        ld c, 15                   ; F_OPEN
        call BDOS
        cp 0FFh
        jp z, err_open

        ld de, dmabuf2
        ld c, 26
        call BDOS

typeloop:
        ld de, dstfcb3
        ld c, 20                   ; F_READ
        call BDOS
        or a
        jp nz, typedone

        ld hl, dmabuf2
        ld b, 128
typechar:
        ld a, (hl)
        cp 1Ah                     ; ^Z marks end of real content
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
        ld de, dstfcb3
        ld c, 16
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

; Prints A (0-9) as a decimal digit. Fine for this demo's small file sizes.
print_digit:
        push af
        add a, '0'
        ld e, a
        ld c, 2
        call BDOS
        pop af
        ret

; Prints one 11-byte (8 name + 3 type) directory-entry image at dirbuf+1
; as "NAME.TYP" + CRLF, trimming trailing spaces from the name field.
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

banner:    db 'CP/M Copy Demo', 13, 10, '$'
hdr_copy:  db 13, 10, 'Copying SOURCE.TXT -> DEST.TXT ...', 13, 10, '$'
hdr_sizes: db 13, 10, 'Sizes (records): ', '$'
sizemsg1:  db ' (SOURCE.TXT), $'
sizemsg2:  db ' (DEST.TXT)', 13, 10, '$'
hdr_dir:   db 13, 10, 'Files in cpm_disk/:', 13, 10, '$'
hdr_type:  db 13, 10, '--- DEST.TXT ---', 13, 10, '$'
crlf:      db 13, 10, '$'

m_makefail:  db 'F_MAKE failed$'
m_writefail: db 'F_WRITE failed$'
m_openfail:  db 'F_OPEN failed$'

message:
        db 'This is a longer message that spans more than one', 13, 10
        db '128-byte CP/M record, to exercise real multi-record', 13, 10
        db 'sequential file I/O rather than a single F_WRITE call.', 13, 10
        db 'This program reads it back one record at a time and', 13, 10
        db 'writes it to a second file, the way a real CP/M', 13, 10
        db 'file-copy utility works.', 13, 10, 26
msgend:
; Pad up to a whole number of 128-byte records - the extra bytes (beyond
; the ^Z above) are never displayed, just written to disk as-is, exactly
; like a real record-oriented file would have trailing slack in its last
; record.
msgpad:    equ ((msgend - message + 127) / 128) * 128 - (msgend - message)
        ds msgpad
msgpadend:
numrecords: equ (msgpadend - message) / 128

srcfcb:  db 0, 'SOURCE  ', 'TXT', 0, 0, 0, 0     ; F_MAKE/F_WRITE/F_CLOSE
        ds 16
        db 0, 0, 0, 0
srcfcb2: db 0, 'SOURCE  ', 'TXT', 0, 0, 0, 0     ; F_OPEN/F_READ (copy source)
        ds 16
        db 0, 0, 0, 0
srcfcb3: db 0, 'SOURCE  ', 'TXT', 0, 0, 0, 0     ; F_SIZE
        ds 16
        db 0, 0, 0, 0

dstfcb:  db 0, 'DEST    ', 'TXT', 0, 0, 0, 0     ; F_MAKE/F_WRITE/F_CLOSE (copy dest)
        ds 16
        db 0, 0, 0, 0
dstfcb2: db 0, 'DEST    ', 'TXT', 0, 0, 0, 0     ; F_SIZE
        ds 16
        db 0, 0, 0, 0
dstfcb3: db 0, 'DEST    ', 'TXT', 0, 0, 0, 0     ; F_OPEN/F_READ (TYPE)
        ds 16
        db 0, 0, 0, 0

searchfcb: db 0, '????????', '???', 0, 0, 0, 0
        ds 16
        db 0, 0, 0, 0

readbuf: ds 128
dmabuf2: ds 128
dirbuf:  ds 128
