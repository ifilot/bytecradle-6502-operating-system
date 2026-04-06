;------------------------------------------------------------------------------
; Hello World Example for ByteCradle 6502
;
; This program prints "Hello World!" to the screen using the ByteCradle OS's
; built-in output routine located at $FFF4.
;------------------------------------------------------------------------------

.PSC02                              ; Assembly for the 65C02 CPU

.import __HEADER_LOAD__             ; Provided by the linker (.cfg) as load address
.include "../../src/jumptable.inc"

.define PUTSTRNL $FFE8              ; Routine to print a null-terminated string with newline

;------------------------------------------------------------------------------
; Program Header (used by ByteCradle OS to determine deployment address)
;------------------------------------------------------------------------------
.segment "HEADER"
.word __HEADER_LOAD__               ; Deployment address (typically $0800), little-endian
.word $0000                         ; Payload length (patched post-link)
.byte BCOS_ABI_MAJOR                ; Required ABI major
.byte BCOS_ABI_MINOR                ; Required ABI minor
.word $0000                         ; CRC16 payload (patched post-link)

;------------------------------------------------------------------------------
; Main Code Segment
;------------------------------------------------------------------------------
.segment "CODE"

start:                              ; Entry point
    lda #<hwstr                     ; Load low byte of string address
    ldx #>hwstr                     ; Load high byte of string address
    jsr PUTSTRNL                    ; Call OS routine to print string with newline
    rts                             ; Return to OS

hwstr:
    .asciiz "Hello World!"          ; Null-terminated string
