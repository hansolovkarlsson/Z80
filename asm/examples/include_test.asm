; Tests INCLUDE: the PRINTMSG macro and BDOS constant come from
; include_defs.inc, not this file.

        include "include_defs.inc"

        org 100h

        PRINTMSG msg
        jp 0

msg:    db 'Included macro works!', 13, 10, '$'
