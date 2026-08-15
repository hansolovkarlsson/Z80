; Tests REPT/ENDM, both at top level and nested inside a MACRO's own
; body (the pattern zexall.mac's real `dss` macro uses: MACRO...ENDM
; wrapping a REPT...ENDM, so the two ENDMs must be told apart correctly
; during macro-body capture - see cpm/docs/ROADMAP.md).
;
; Also proves the REPT count can be $-dependent: FILLTO pads out to a
; target address using "target-$" as the repeat count, exactly like
; zexall.mac's tstr/tmsg macros pad fields to a fixed size.

BDOS: equ 5

; Macro wrapping REPT internally, mirroring zexall.mac's `dss`.
DSS MACRO bytes,value
        rept bytes
        db value
        endm
ENDM

; Pads with 'X' bytes up to (but not including) `target`.
FILLTO MACRO target
        DSS target-$,'X'
ENDM

        org 100h

start:
        ; Top-level REPT: five NOPs, directly.
        rept 5
        nop
        endm

        ; Macro-wrapped REPT via DSS, fixed count.
        DSS 3,'!'

        ; $-dependent REPT count via FILLTO.
        ld de, msg
        ld c, 9
        call BDOS
        jp 0

        FILLTO 0140h

msg:    db 'REPT ok', 13, 10, '$'
