.importzp sp
.export _fs_syscall_stack_reset

.segment "CODE"

_fs_syscall_stack_reset:
    pha
    txa
    pha
    lda #<$0800
    sta sp
    lda #>$0800
    sta sp+1
    pla
    tax
    pla
    rts
