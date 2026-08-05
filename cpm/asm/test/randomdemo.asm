; A small "real" CP/M utility demonstrating random-access file I/O
; (F_READRAND/F_WRITERAND/F_WRITEZF, BDOS 33/34/40) - the one part of the
; file I/O surface neither file_test.asm, filedemo.asm, nor copydemo.asm
; exercises at all (they're all purely sequential). Not a pass/fail
; regression check; meant to be run and read by hand.
;
; Writes three short labels into RANDOM.DAT at record numbers 5, 1, and 3
; - deliberately out of order and with gaps - using F_WRITERAND/F_WRITEZF.
; Since the host filesystem zero-fills a file when a write seeks past
; current end-of-file (see cpm.c's File I/O comment), the untouched
; records (0, 2, 4) read back as all zero bytes: real CP/M random access
; works exactly this way over real disk blocks, and this demo shows the
; same "gaps read as empty" behavior falls out naturally here too.
;
;   bin/z80asm cpm/asm/test/randomdemo.asm -o randomdemo.com
;   bin/z80 randomdemo.com

BDOS:   equ 5

        org 100h

start:
        ld de, banner
        ld c, 9
        call BDOS

        ld de, fcb
        ld c, 22                 ; F_MAKE
        call BDOS
        cp 0FFh
        jp z, err_make

        ld de, dmabuf
        ld c, 26                 ; F_DMAOFF
        call BDOS

        ; --- Write record 5 ("FIVE") via F_WRITEZF - the write that
        ; extends the file, most literally exercising the "zero-fill the
        ; skipped records" behavior the function's named for. ---
        ld a, 5
        call setrandrec
        ld hl, lbl_five
        call fillrecord
        ld de, fcb
        ld c, 40                  ; F_WRITEZF
        call BDOS
        or a
        jp nz, err_write

        ; --- Write record 1 ("ONE") via plain F_WRITERAND ---
        ld a, 1
        call setrandrec
        ld hl, lbl_one
        call fillrecord
        ld de, fcb
        ld c, 34                  ; F_WRITERAND
        call BDOS
        or a
        jp nz, err_write

        ; --- Write record 3 ("THREE") via plain F_WRITERAND ---
        ld a, 3
        call setrandrec
        ld hl, lbl_three
        call fillrecord
        ld de, fcb
        ld c, 34                  ; F_WRITERAND
        call BDOS
        or a
        jp nz, err_write

        ld de, fcb
        ld c, 16                  ; F_CLOSE
        call BDOS

        ; --- Read back records 0-5 in order via F_READRAND, showing
        ; which are real content and which are the untouched gaps. ---
        ld de, fcb2
        ld c, 15                  ; F_OPEN
        call BDOS
        cp 0FFh
        jp z, err_open

        ld de, dmabuf2
        ld c, 26
        call BDOS

        ; Tracked in memory, not a register: print9/print_digit both load
        ; C with a BDOS function number internally, so a register would
        ; get clobbered by the very calls this loop makes to print the
        ; record number it's tracking.
        xor a
        ld (recnum), a
readloop:
        ld a, (recnum)
        ld de, fcb2
        call setrandrec2

        ld de, fcb2
        ld c, 33                  ; F_READRAND
        call BDOS

        ld de, msg_record
        call print9
        ld a, (recnum)
        call print_digit
        ld de, msg_colon
        call print9

        ld a, (dmabuf2)
        or a
        jp z, showgap

        ld hl, dmabuf2
        ld b, 8
showlabel:
        ld a, (hl)
        cp ' '
        jp z, showdone
        push bc
        push hl
        ld e, a
        ld c, 2
        call BDOS
        pop hl
        pop bc
        inc hl
        djnz showlabel
        jp showdone
showgap:
        ld de, msg_empty
        call print9
showdone:
        ld de, crlf
        ld c, 9
        call BDOS

        ld a, (recnum)
        inc a
        ld (recnum), a
        cp 6
        jp nz, readloop

        ld de, fcb2
        ld c, 16                  ; F_CLOSE
        call BDOS

        ; --- F_SIZE: confirm the file is 6 records (the highest record
        ; number touched, 5, plus one) ---
        ld de, fcb3
        ld c, 35                  ; F_SIZE
        call BDOS
        ld de, msg_size
        call print9
        ld a, (fcb3 + 21h)
        call print_digit
        ld de, crlf
        ld c, 9
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

; Prints A (0-9) as a decimal digit.
print_digit:
        push af
        add a, '0'
        ld e, a
        ld c, 2
        call BDOS
        pop af
        ret

; Sets fcb's R0 (and R1/R2=0) to the record number in A.
setrandrec:
        push de
        ld de, fcb
        call setrandrec2
        pop de
        ret

; Sets (DE)'s R0/R1/R2 fields to the record number in A. DE = FCB address.
setrandrec2:
        push hl
        ld hl, 21h
        add hl, de
        ld (hl), a
        inc hl
        ld (hl), 0
        inc hl
        ld (hl), 0
        pop hl
        ret

; Fills dmabuf (128 bytes) with the ^Z-padded 8-char label at (HL).
fillrecord:
        push af
        push bc
        push de
        push hl
        ld de, dmabuf
        ld b, 128
        ld a, 1Ah
fillpad:
        ld (de), a
        inc de
        djnz fillpad
        pop hl
        push hl
        ld de, dmabuf
        ld b, 8
copylabel:
        ld a, (hl)
        or a
        jp z, copydone
        ld (de), a
        inc hl
        inc de
        djnz copylabel
copydone:
        pop hl
        pop de
        pop bc
        pop af
        ret

banner:    db 'CP/M Random Access Demo', 13, 10, '$'
msg_record: db 'record $'
msg_colon:  db ': $'
msg_empty:  db '(empty)$'
msg_size:   db 13, 10, 'File size: $'
crlf:       db 13, 10, '$'

m_makefail:  db 'F_MAKE failed$'
m_writefail: db 'F_WRITE failed$'
m_openfail:  db 'F_OPEN failed$'

lbl_one:   db 'ONE', 0
lbl_three: db 'THREE', 0
lbl_five:  db 'FIVE', 0

fcb:    db 0, 'RANDOM  ', 'DAT', 0, 0, 0, 0     ; F_MAKE/F_WRITERAND/F_WRITEZF/F_CLOSE
        ds 16
        db 0, 0, 0, 0
fcb2:   db 0, 'RANDOM  ', 'DAT', 0, 0, 0, 0     ; F_OPEN/F_READRAND/F_CLOSE
        ds 16
        db 0, 0, 0, 0
fcb3:   db 0, 'RANDOM  ', 'DAT', 0, 0, 0, 0     ; F_SIZE
        ds 16
        db 0, 0, 0, 0

recnum:  db 0

dmabuf:  ds 128
dmabuf2: ds 128
