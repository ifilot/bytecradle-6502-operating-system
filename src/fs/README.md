# Filesystem Module Layout And Bank Policy

BCOS filesystem and shell code are split between resident ROM (`CODE`) and
banked ROM (`B2CODE`, `B3CODE`, `B4CODE`). This split is part of the API
design, not just a linker detail.

## Placement Rule

Resident `CODE` owns:

- public BCOS/userland ABI entrypoints
- bank-switch wrappers
- request-block validation and result marshaling
- very small kernel entrypoints that are called continuously

Banked `B2CODE` owns:

- SD-card command and sector I/O internals
- MBR/BPB parsing
- raw FAT and directory scanning
- cluster-chain walking
- file descriptor read mechanics
- read-side stat/readdir mechanics

Banked `B3CODE` owns write-side filesystem work:

- FAT mutation and cluster allocation
- directory slot allocation and extension
- mkdir/touch/create implementations
- sequential file writes and flush metadata updates

Banked `B4CODE` owns shell/command processing:

- command parsing and dispatch
- built-in shell commands
- `.COM` loading and launch validation
- interactive text presentation that is large but not resident-critical

In short:

```text
Resident CODE: ABI, tiny dispatch wrappers, low-level shared kernel code
B2CODE:       SD/FAT read-side mechanics
B3CODE:       FAT write-side mechanics
B4CODE:       shell and command processor
```

## ABI Boundary

User programs and BCOS are separate cc65 programs. They do not share the same
cc65 argument stack in zero page. Therefore, userland jump-table calls must use
safe ABI shapes:

- zero arguments
- one argument in registers
- one pointer to a request block in user memory

Do not expose raw multi-argument C functions through the jump table unless
dedicated assembly glue marshals the arguments.

BCOS zero page is intentionally capped at `$20-$4F`; user programs currently
start their cc65 zero page at `$50`. Keep this boundary intact when changing
linker configs, otherwise a syscall can corrupt the user program's runtime
stack pointer.

Examples:

- `fs_open(const char*)` is safe: one pointer argument.
- `fs_read_byte(fs_fd_t)` is safe: one scalar argument.
- `fs_read_block(bcos_fs_rw_t*)` is safe: one request-block pointer.
- `fs_read(fd, dst, len)` is not a safe userland ABI call and remains internal.

## Current Files

- `fat32.c`: resident public wrappers and shared FAT state
- `fat32_ops.c`: read-side filesystem operations, descriptor reads, stat/readdir
- `fat32_core.c`: MBR/BPB parsing, directory reads, search, cluster chains
- `fat32_alloc.c`: write-side FAT allocation and directory extension
- `fat32_create.c`: write-side `mkdir`/`touch`/`create`
- `fat32_write.c`: sequential write and flush implementation
- `fat32_ui.c`: resident partition/listing formatting
- `sdcard.c`: public SD wrappers plus banked SD sector operations
- `sdcmds.s`: banked low-level SD command transport
- `crc16sd.s`: SD-sector CRC helper

## Before Adding A New Function

Ask:

- Is it a userland ABI or request-block marshaller? Put it in resident `CODE`.
- Does it touch sectors, FAT entries, clusters, or on-disk directory entries?
  Put read-only logic in `B2CODE` and mutating logic in `B3CODE`.
- Does it parse shell commands, launch `.COM` files, or format large interactive
  shell output? Put it in `B4CODE`.
- Is it large and rarely called? Prefer banked code.
- Does it require multiple user arguments? Use a request block.

Always check bank sizes after changes:

```bash
make -C src
python3 scripts/inspect_bcos_size.py --map src/bin/sbc.map --bin src/bin/bcos.bin --out /tmp/bcos-size-report.md
```
