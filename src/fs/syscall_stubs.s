.import _fs_chdir, _fs_getcwd_user, _fs_open, _fs_open_ex, _fs_read_byte, _fs_read_block
.import _fs_stat, _fs_readdir, _fs_create, _fs_write_block
.import _fs_flush, _fs_close
.import _fs_syscall_stack_reset
.import _fs_create_impl

.export _fs_chdir_syscall
.export _fs_getcwd_user_syscall
.export _fs_open_syscall
.export _fs_open_ex_syscall
.export _fs_read_byte_syscall
.export _fs_read_block_syscall
.export _fs_stat_syscall
.export _fs_readdir_syscall
.export _fs_create_syscall
.export _fs_write_block_syscall
.export _fs_flush_syscall
.export _fs_close_syscall

.include "constants.inc"

.segment "BSS"
saved_rombank:
    .res 1

.segment "CODE"

_fs_chdir_syscall:
    jsr _fs_syscall_stack_reset
    jmp _fs_chdir

_fs_getcwd_user_syscall:
    jsr _fs_syscall_stack_reset
    jmp _fs_getcwd_user

_fs_open_syscall:
    jsr _fs_syscall_stack_reset
    jmp _fs_open

_fs_open_ex_syscall:
    jsr _fs_syscall_stack_reset
    jmp _fs_open_ex

_fs_read_byte_syscall:
    jsr _fs_syscall_stack_reset
    jmp _fs_read_byte

_fs_read_block_syscall:
    jsr _fs_syscall_stack_reset
    jmp _fs_read_block

_fs_stat_syscall:
    jsr _fs_syscall_stack_reset
    jmp _fs_stat

_fs_readdir_syscall:
    jsr _fs_syscall_stack_reset
    jmp _fs_readdir

_fs_create_syscall:
    jsr _fs_syscall_stack_reset
    pha
    txa
    pha
    lda ROMBANKREGISTER
    sta saved_rombank
    lda #$03
    sta ROMBANKREGISTER
    pla
    tax
    pla
    jsr _fs_create_impl
    pha
    lda saved_rombank
    sta ROMBANKREGISTER
    pla
    ldx #$00
    cmp #$80
    bcc @done
    dex
@done:
    rts

_fs_write_block_syscall:
    jsr _fs_syscall_stack_reset
    jmp _fs_write_block

_fs_flush_syscall:
    jsr _fs_syscall_stack_reset
    jmp _fs_flush

_fs_close_syscall:
    jsr _fs_syscall_stack_reset
    jmp _fs_close
