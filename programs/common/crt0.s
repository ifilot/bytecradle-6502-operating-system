; ---------------------------------------------------------------------------
; crt0.s
; Shared C runtime entrypoint for BCOS user programs.
; ---------------------------------------------------------------------------

.import __HEADER_LOAD__                     ; import start location

.export   _init, _exit

.export   __STARTUP__ : absolute = 1        ; Mark as startup
.import   __RAM_START__, __RAM_SIZE__       ; Linker generated

.import    copydata, zerobss, initlib, donelib
.import    _main, pusha0

.include  "zeropage.inc"
.include "../../src/jumptable.inc"

; ---------------------------------------------------------------------------

.segment "HEADER"

.word __HEADER_LOAD__
.word $0000
.byte BCOS_ABI_MAJOR
.byte BCOS_ABI_MINOR
.word $0000

; ---------------------------------------------------------------------------

.segment  "STARTUP"

; ---------------------------------------------------------------------------
; Set cc65 argument stack pointer
; ---------------------------------------------------------------------------
_init:

          LDA     #<(__RAM_START__ + __RAM_SIZE__)
          STA     sp
          LDA     #>(__RAM_START__ + __RAM_SIZE__)
          STA     sp+1

; ---------------------------------------------------------------------------
; Initialize memory storage
; ---------------------------------------------------------------------------

          JSR     zerobss              ; Clear BSS segment
          JSR     copydata             ; Initialize DATA segment
          JSR     initlib              ; Run constructors

; ---------------------------------------------------------------------------
; Call main(argc, argv)
; ---------------------------------------------------------------------------

          LDA     $0600                ; argc
          JSR     pusha0               ; push argc (16-bit) on cc65 stack
          LDX     #$06                 ; argv high byte
          LDA     #$01                 ; argv low byte
          JSR     _main

; ---------------------------------------------------------------------------
; Back from main (this is also the _exit entry)
; ---------------------------------------------------------------------------

_exit:    JSR     donelib              ; Run destructors
          RTS
