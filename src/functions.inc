;
; Global symbol imports used across multiple .s files
; This file should be included to access shared subroutines
;

.import _getch
.import _putch
.import _puthex
.import bcos_get_abi_major
.import bcos_get_abi_minor
.import bcos_reserved
.import char2nibble
.import char2num
.import chartoupper
.import clearline
.import _fs_chdir
.import _fs_chdir_syscall
.import _fs_close
.import _fs_close_syscall
.import _fs_create
.import _fs_create_syscall
.import _fs_flush
.import _fs_flush_syscall
.import _fs_getcwd_user
.import _fs_getcwd_user_syscall
.import _fs_open
.import _fs_open_ex
.import _fs_open_ex_syscall
.import _fs_open_syscall
.import _fs_read_block
.import _fs_read_block_syscall
.import _fs_read_byte
.import _fs_read_byte_syscall
.import _fs_readdir
.import _fs_readdir_syscall
.import _fs_stat
.import _fs_stat_syscall
.import _fs_write_block
.import _fs_write_block_syscall
.import getch
.import getpos
.import ischarvalid
.import isr
.import makecrctable
.import newcmdline
.import newline
.import printnibble
.import putch
.import putdec
.import putds
.import puthex
.import putspace
.import putstr
.import putstrnl
.import puttab
