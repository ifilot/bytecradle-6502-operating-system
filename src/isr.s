;-------------------------------------------------------------------------------
; INTERRUPT SERVICE ROUTINES
;-------------------------------------------------------------------------------

.include "constants.inc"

.export isr

.segment "CODE"
.PSC02

;-------------------------------------------------------------------------------
; Interrupt Service Routine
;
; Checks whether a character is available over the UART and stores it in the
; key buffer to be parsed later.
;-------------------------------------------------------------------------------
isr:
    pha             ; put A on stack
    lda ACIA_STAT   ; check status
    and #$08        ; check for bit 3
    beq isr_exit    ; if not set, exit isr
    lda ACIA_DATA   ; load byte
    phx
    pha             ; preserve received char while testing buffer state
    ldx TBPR        ; load write pointer index
    inx             ; probe next write pointer
    cpx TBPL        ; if equal to read pointer, buffer is full
    bne @store_char
    inc TBPL        ; discard oldest unread byte to make room
@store_char:
    dex             ; restore current write pointer
    pla             ; restore received character
    sta TB,x        ; store character
    inc TBPR        ; advance write pointer
    plx             ; recover X
isr_exit:
    pla             ; recover A
    rti
