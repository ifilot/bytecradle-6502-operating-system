# ByteCradle Userland Filesystem ABI

BCOS exposes a small filesystem ABI to `.COM` programs through the jump table.
The ABI is intentionally conservative: simple calls use zero or one argument,
and richer calls use a single pointer to a request block in user memory.

## Why Request Blocks?

BCOS and userland programs are separate cc65 programs. They do not share the
same cc65 C stack pointer in zero page, so multi-argument C calls through the
jump table are not a safe ABI boundary.

Safe patterns are:

- zero-argument calls
- one-argument calls passed in registers
- one pointer to a user-owned request block

Avoid exporting new jump-table calls with multiple scalar arguments.

## ABI Version

The current filesystem ABI is `1.1`. User programs should include the generated
header and either check `BCOS_ABI_COMPATIBLE()` or at least require
`bcos_abi_major() == BCOS_ABI_MAJOR`.

## Current Calls

The generated public header is `programs/.generated/io.h`.

```c
uint8_t fs_chdir(const char* path);
uint8_t fs_getcwd(bcos_fs_getcwd_t* req);
fs_fd_t fs_open(const char* filename);
uint8_t fs_open_ex(bcos_fs_open_t* req);
int16_t fs_read_byte(fs_fd_t fd);
uint8_t fs_read_block(bcos_fs_rw_t* req);
uint8_t fs_stat(bcos_fs_stat_t* req);
uint8_t fs_readdir(bcos_fs_dirent_t* req);
fs_fd_t fs_create(const char* filename);
uint8_t fs_write_block(bcos_fs_rw_t* req);
uint8_t fs_flush(fs_fd_t fd);
void fs_close(fs_fd_t fd);
```

`fs_read_byte` returns `0..255` for a byte and `-1` for EOF or error.

`fs_open` and `fs_create` remain compact convenience calls. New code that wants
structured status reporting should prefer `fs_open_ex`.

## Status Codes

```c
#define BCOS_OK             0x00u
#define BCOS_ERR_GENERIC    0xFFu
#define BCOS_ERR_NOT_FOUND  0xFEu
#define BCOS_ERR_BAD_NAME   0xFCu
#define BCOS_ERR_EOF        0xFBu
#define BCOS_ERR_BAD_FD     0xFAu
```

## Attributes

```c
#define BCOS_FS_ATTR_DIR    0x10u
```

Other attribute bits currently match FAT directory entry attributes.

## Current Directory

```c
typedef struct bcos_fs_getcwd {
    char* buffer;
    uint8_t length;
    uint8_t status;
} bcos_fs_getcwd_t;
```

Example:

```c
char cwd_buf[32];
bcos_fs_getcwd_t cwd;

cwd.buffer = cwd_buf;
cwd.length = sizeof(cwd_buf);
cwd.status = BCOS_ERR_GENERIC;

if(fs_getcwd(&cwd) == BCOS_OK) {
    /* cwd_buf contains strings such as ":/> " or ":/PROGRAMS/> " */
}
```

The buffer is always null-terminated on success. Too-small buffers return
`BCOS_ERR_GENERIC`.

## Extended Open

```c
#define BCOS_FS_OPEN_READ   0x01u
#define BCOS_FS_OPEN_CREATE 0x02u

typedef struct bcos_fs_open {
    const char* path;
    uint8_t mode;
    fs_fd_t fd;
    uint8_t status;
} bcos_fs_open_t;
```

Example:

```c
bcos_fs_open_t open_req;

open_req.path = "README.TXT";
open_req.mode = BCOS_FS_OPEN_READ;
open_req.fd = -1;
open_req.status = BCOS_ERR_GENERIC;

if(fs_open_ex(&open_req) == BCOS_OK) {
    /* open_req.fd is ready for fs_read_byte/fs_read_block */
}
```

`BCOS_FS_OPEN_READ` returns `BCOS_ERR_NOT_FOUND` for missing files and
`BCOS_ERR_BAD_NAME` for invalid FAT 8.3 names. `BCOS_FS_OPEN_CREATE` creates a
new sequential-write descriptor, equivalent to `fs_create` but with request
block status and fd output.

## Block Read

```c
typedef struct bcos_fs_rw {
    fs_fd_t fd;
    uint8_t* buffer;
    uint16_t length;
    uint16_t actual;
    uint8_t status;
} bcos_fs_rw_t;
```

Example:

```c
uint8_t buffer[64];
bcos_fs_rw_t req;

req.fd = fd;
req.buffer = buffer;
req.length = sizeof(buffer);
req.actual = 0;
req.status = BCOS_ERR_GENERIC;

if(fs_read_block(&req) == BCOS_OK) {
    /* req.actual bytes are valid in buffer */
}
```

`fs_read_block` advances the descriptor offset. A zero-byte read returns
`BCOS_ERR_EOF`.

## Create And Block Write

`fs_create(filename)` creates a new file in the current directory and opens it
for sequential writing. Duplicate names fail; existing files are not truncated.

`fs_write_block` uses the same `bcos_fs_rw_t` request block as `fs_read_block`:

```c
bcos_fs_rw_t req;

req.fd = fd;
req.buffer = buffer;
req.length = count;
req.actual = 0;
req.status = BCOS_ERR_GENERIC;

if(fs_write_block(&req) == BCOS_OK) {
    /* req.actual bytes were accepted */
}
```

Writes advance the descriptor offset. `fs_flush(fd)` writes the directory
metadata for a writable descriptor. `fs_close(fd)` also flushes non-empty
writable files before closing.

## Stat

```c
typedef struct bcos_fs_stat {
    const char* path;
    uint8_t attrib;
    uint32_t size;
    uint32_t cluster;
    uint8_t status;
} bcos_fs_stat_t;
```

`path` is a direct child entry in the current directory, using FAT 8.3 naming.
On success, `attrib`, `size`, and `cluster` are populated.

## Directory Entries

```c
typedef struct bcos_fs_dirent {
    uint8_t index;
    char name[12];
    uint8_t attrib;
    uint32_t size;
    uint32_t cluster;
    uint8_t status;
} bcos_fs_dirent_t;
```

Set `index` to the entry number in the current directory. On success, `name`
is a null-terminated short name such as `README.TXT` or `PROGRAMS`.

When `index` is past the last entry, `fs_readdir` returns `BCOS_ERR_EOF`.

## Current Limitations

- Paths are direct children of the current directory; full path traversal is
  not part of this ABI yet.
- File descriptors do not support seek, append, truncate, rename, or delete.
- Writes are sequential and only available through `fs_create`.
- `fs_readdir` uses an 8-bit index, matching the current `MAXFILES` limit.
- Long file names are skipped; only FAT 8.3 entries are exposed.

## Adding Future Calls

Prefer request-block APIs. For example, future open modes should likely use a
single `bcos_fs_open_t*` request rather than multiple scalar arguments.

Do not expose multi-argument C signatures directly through the jump table unless
the ABI glue explicitly marshals arguments.

Keep the OS and userland zero-page regions disjoint. BCOS reserves `$20-$4F`;
user programs currently start their cc65 zero page at `$50`.

See also `src/fs/README.md` for the resident-vs-banked placement policy.
