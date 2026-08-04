; Exercises the BDOS file functions added to emu/src/cpm.c per
; docs/CPM_REFERENCE.md: F_MAKE (22), F_WRITE (21), F_CLOSE (16),
; F_RENAME (23), F_OPEN (15), F_READ (20), F_SFIRST (17), F_DELETE (19),
; F_WRITERAND (34), F_READRAND (33). Every drive/user is mapped onto a
; single host directory (cpm_disk/, created next to wherever bin/z80 is
; run from) - see cpm.c's File I/O comment. Same "OK n"/"FAIL n"
; convention as selftest.asm/gaps_test.asm.

BDOS:   equ 5

        org 100h

start:
        ; --- Check 1: F_MAKE, F_WRITE one record, F_CLOSE all succeed ---
        ld hl, msg                ; fill the record we're about to write
        ld de, dmabuf
        ld b, msglen
fillloop:
        ld a, (hl)
        ld (de), a
        inc hl
        inc de
        djnz fillloop

        ld de, dmabuf
        ld c, 26                 ; F_DMAOFF
        call BDOS

        ld de, fcb
        ld c, 22                 ; F_MAKE
        call BDOS
        or a
        jp nz, fail1

        ld de, fcb
        ld c, 21                 ; F_WRITE
        call BDOS
        or a
        jp nz, fail1

        ld de, fcb
        ld c, 16                 ; F_CLOSE
        call BDOS
        or a
        jp nz, fail1

        ld de, ok1
        call print
        jp check2
fail1:  ld de, bad1
        call print

check2:
        ; --- Check 2: F_RENAME (HELLO.TXT -> WORLD.TXT) succeeds ---
        ld de, fcb                ; old name at FCB, new name at FCB+16
        ld c, 23                  ; F_RENAME
        call BDOS
        or a
        jp nz, fail2
        ld de, ok2
        call print
        jp check3
fail2:  ld de, bad2
        call print

check3:
        ; --- Check 3: F_OPEN the renamed file, F_READ it back, verify content ---
        ld de, fcb2
        ld c, 15                  ; F_OPEN
        call BDOS
        or a
        jp nz, fail3

        ld de, dmabuf2
        ld c, 26                  ; F_DMAOFF
        call BDOS

        ld de, fcb2
        ld c, 20                  ; F_READ
        call BDOS
        or a
        jp nz, fail3

        ld hl, dmabuf2
        ld de, msg
        ld b, msglen
cmploop:
        ld a, (de)
        cp (hl)
        jp nz, fail3
        inc hl
        inc de
        djnz cmploop

        ld de, fcb2
        ld c, 16                  ; F_CLOSE
        call BDOS

        ld de, ok3
        call print
        jp check4
fail3:  ld de, bad3
        call print

check4:
        ; --- Check 4: F_SFIRST with a wildcarded name finds WORLD.TXT ---
        ld de, searchfcb
        ld c, 17                  ; F_SFIRST
        call BDOS
        or a
        jp nz, fail4

        ; BDOS wrote the matched directory entry to the current DMA
        ; address (dmabuf2, still set from check 3): bytes 1-8 = name.
        ld hl, dmabuf2 + 1
        ld de, worldname
        ld b, 8
cmploop2:
        ld a, (de)
        cp (hl)
        jp nz, fail4
        inc hl
        inc de
        djnz cmploop2

        ld de, ok4
        call print
        jp check5
fail4:  ld de, bad4
        call print

check5:
        ; --- Check 5: F_DELETE removes it; a subsequent F_OPEN then fails ---
        ld de, fcb2
        ld c, 19                  ; F_DELETE
        call BDOS
        or a
        jp nz, fail5

        ld de, fcb2
        ld c, 15                  ; F_OPEN (should now fail: 0FFh)
        call BDOS
        cp 0FFh
        jp nz, fail5

        ld de, ok5
        call print
        jp check6
fail5:  ld de, bad5
        call print

check6:
        ; --- Check 6: F_WRITERAND (34) then F_READRAND (33) on an FCB
        ; that's been F_CLOSEd, with no F_OPEN in between, both succeed.
        ; Real CP/M's random I/O works directly off the FCB's own
        ; EX/S1/S2 fields, which a Close doesn't erase - a program is not
        ; required to re-open before reusing the same FCB for random
        ; access. A real Ashton-Tate dBASE II binary relies on exactly
        ; this on QUIT (re-reading a .DBF header it had already closed
        ; during CREATE); before find_or_reopen_file() existed in cpm.c,
        ; this failed with error 9 ("unopened FCB"), which dBASE surfaced
        ; to the user as "Disk is full".
        ld hl, msg6
        ld de, dmabuf
        ld b, msg6len
fillloop6:
        ld a, (hl)
        ld (de), a
        inc hl
        inc de
        djnz fillloop6

        ld de, dmabuf
        ld c, 26                  ; F_DMAOFF
        call BDOS

        ld de, fcb6
        ld c, 22                  ; F_MAKE
        call BDOS
        or a
        jp nz, fail6

        xor a                      ; R0-R2 = 0: random record 0
        ld (fcb6+33), a
        ld (fcb6+34), a
        ld (fcb6+35), a
        ld de, fcb6
        ld c, 34                  ; F_WRITERAND
        call BDOS
        or a
        jp nz, fail6

        ld de, fcb6
        ld c, 16                  ; F_CLOSE - no F_OPEN follows this
        call BDOS
        or a
        jp nz, fail6

        ld de, dmabuf2
        ld c, 26                  ; F_DMAOFF
        call BDOS

        xor a
        ld (fcb6+33), a
        ld (fcb6+34), a
        ld (fcb6+35), a
        ld de, fcb6
        ld c, 33                  ; F_READRAND on the still-closed FCB
        call BDOS
        or a
        jp nz, fail6

        ld hl, dmabuf2
        ld de, msg6
        ld b, msg6len
cmploop6:
        ld a, (de)
        cp (hl)
        jp nz, fail6
        inc hl
        inc de
        djnz cmploop6

        ld de, fcb6
        ld c, 19                  ; F_DELETE (cleanup)
        call BDOS

        ld de, ok6
        call print
        jp done
fail6:  ld de, bad6
        call print

done:   jp 0

; DE = message pointer, BDOS print string + trailing CRLF, then return.
print:  ld c, 9
        call BDOS
        ld de, crlf
        ld c, 9
        call BDOS
        ret

msg:    db 'Hello, CP/M file I/O!'
msglen: equ $ - msg

; F_RENAME reads the new name's F1-T3 fields starting at FCB+17 - that's
; inside the FCB's own AL field (offset 16-31, unused before the file is
; open), so the "new name" block below *is* those 16 bytes, not appended
; after the FCB: DR-equivalent(1) + F1-F8(8) + T1-T3(3) + 4 unused pad
; bytes = 16, landing exactly on offset 16-31.
fcb:    db 0,'HELLO   ','TXT',0,0,0,0     ; offset 0-15: old name (DR..RC)
        db 0,'WORLD   ','TXT',0,0,0,0     ; offset 16-31: new name (F_RENAME)
        db 0,0,0,0                        ; offset 32-35: CR,R0,R1,R2

fcb2:   db 0,'WORLD   ','TXT',0,0,0,0     ; renamed file (F_OPEN/F_READ/F_DELETE)
        ds 16
        db 0,0,0,0

searchfcb: db 0,'????????','TXT',0,0,0,0  ; wildcard: any 8-char name, .TXT
        ds 16
        db 0,0,0,0

worldname: db 'WORLD   '

fcb6:   db 0,'RANDIO  ','TXT',0,0,0,0
        ds 16
        db 0,0,0,0

ok1:    db 'OK 1 F_MAKE / F_WRITE / F_CLOSE$'
bad1:   db 'FAIL 1 F_MAKE / F_WRITE / F_CLOSE$'
ok2:    db 'OK 2 F_RENAME$'
bad2:   db 'FAIL 2 F_RENAME$'
ok3:    db 'OK 3 F_OPEN / F_READ round-trip$'
bad3:   db 'FAIL 3 F_OPEN / F_READ round-trip$'
ok4:    db 'OK 4 F_SFIRST wildcard search$'
bad4:   db 'FAIL 4 F_SFIRST wildcard search$'
ok5:    db 'OK 5 F_DELETE$'
bad5:   db 'FAIL 5 F_DELETE$'
ok6:    db 'OK 6 F_WRITERAND / F_READRAND on a closed FCB$'
bad6:   db 'FAIL 6 F_WRITERAND / F_READRAND on a closed FCB$'
crlf:   db 13, 10, '$'

msg6:   db 'Random I/O on a closed FCB'
msg6len: equ $ - msg6

dmabuf:  ds 128
dmabuf2: ds 128
