#include <stdint.h>
#include <string.h>

#include "io.h"

static uint8_t check(uint8_t cond, const char* name) {
    putstr(cond ? "PASS " : "FAIL ");
    putstrnl(name);
    return cond ? 0 : 1;
}

static uint8_t expected_big_byte(uint16_t offset) {
    return (uint8_t)('A' + (offset % 26));
}

static uint8_t expected_write_byte(uint16_t offset) {
    return (uint8_t)('a' + (offset % 26));
}

int main(void) {
    fs_fd_t fd;
    fs_fd_t fds[4];
    uint8_t buf[96];
    static uint8_t bigbuf[610];
    uint8_t i;
    uint16_t j;
    int16_t b;
    bcos_fs_rw_t rw;
    bcos_fs_stat_t st;
    bcos_fs_dirent_t de;
    bcos_fs_getcwd_t cwd;
    bcos_fs_open_t op;
    uint8_t failures = 0;

    if((uint8_t)(bcos_abi_major() ^ BCOS_ABI_MAJOR) != 0u) {
        putstrnl("FAIL abi");
        return 1;
    }

    failures += check(fs_chdir("/") == 0, "chdir-root");
    cwd.buffer = (char*)buf;
    cwd.length = sizeof(buf);
    cwd.status = 0xFF;
    failures += check(fs_getcwd(&cwd) == BCOS_OK && memcmp(buf, ":/> ", 4) == 0, "getcwd-root");

    fd = fs_open("README.TXT");
    failures += check(fd >= 0, "open-readme");
    if(fd >= 0) {
        for(i = 0; i < 31; i++) {
            b = fs_read_byte(fd);
            buf[i] = (uint8_t)b;
            if(b < 0) {
                break;
            }
        }
        failures += check(i == 31 && memcmp(buf, "ByteCradle SD image generated b", 31) == 0, "read-sequential");
        fs_close(fd);
    }

    fd = fs_open("README.TXT");
    failures += check(fd >= 0, "open-readme-block");
    if(fd >= 0) {
        rw.fd = fd;
        rw.buffer = buf;
        rw.length = 31;
        rw.actual = 0;
        rw.status = 0xFF;
        failures += check(fs_read_block(&rw) == BCOS_OK, "block-status");
        failures += check(rw.actual == 31 && memcmp(buf, "ByteCradle SD image generated b", 31) == 0, "block-read");

        rw.buffer = buf;
        rw.length = 20;
        rw.actual = 0;
        rw.status = 0xFF;
        failures += check(fs_read_block(&rw) == BCOS_OK, "block-advance-status");
        failures += check(rw.actual == 20 && memcmp(buf, "y emulator/script/cr", 20) == 0, "block-advance");
        fs_close(fd);
    }

    for(i = 0; i < 4; i++) {
        fds[i] = fs_open("README.TXT");
        if(fds[i] < 0) {
            break;
        }
    }
    failures += check(i == 4, "multi-open");
    fd = fs_open("README.TXT");
    failures += check(fd < 0, "open-exhausted");
    while(i > 0) {
        --i;
        fs_close(fds[i]);
    }

    fd = fs_open("README.TXT");
    failures += check(fd >= 0, "open-close-read");
    if(fd >= 0) {
        fs_close(fd);
        failures += check(fs_read_byte(fd) < 0, "read-after-close");
    }

    fd = fs_open("BIG.TXT");
    failures += check(fd >= 0, "open-big");
    if(fd >= 0) {
        rw.fd = fd;
        rw.buffer = bigbuf;
        rw.length = 520;
        rw.actual = 0;
        rw.status = 0xFF;
        failures += check(fs_read_block(&rw) == BCOS_OK && rw.actual == 520, "big-read");
        failures += check(bigbuf[0] == expected_big_byte(0) &&
                          bigbuf[25] == expected_big_byte(25) &&
                          bigbuf[26] == expected_big_byte(26) &&
                          bigbuf[511] == expected_big_byte(511) &&
                          bigbuf[512] == expected_big_byte(512) &&
                          bigbuf[519] == expected_big_byte(519), "big-boundary");

        rw.length = 80;
        rw.actual = 0;
        rw.status = 0xFF;
        failures += check(fs_read_block(&rw) == BCOS_OK &&
                          rw.actual == 80 &&
                          bigbuf[0] == expected_big_byte(520) &&
                          bigbuf[79] == expected_big_byte(599), "big-advance");

        rw.length = 1;
        rw.actual = 1;
        rw.status = 0;
        failures += check(fs_read_block(&rw) == BCOS_ERR_EOF && rw.actual == 0, "big-eof");
        fs_close(fd);
    }

    fd = fs_open("EMPTY.TXT");
    failures += check(fd >= 0, "open-empty");
    if(fd >= 0) {
        b = fs_read_byte(fd);
        failures += check(b < 0, "read-empty");
        rw.fd = fd;
        rw.buffer = buf;
        rw.length = sizeof(buf);
        rw.actual = 1;
        rw.status = 0;
        failures += check(fs_read_block(&rw) == BCOS_ERR_EOF && rw.actual == 0, "block-empty");
        fs_close(fd);
    }

    fd = fs_open("NOFILE.TXT");
    failures += check(fd < 0, "open-missing");

    op.path = "README.TXT";
    op.mode = BCOS_FS_OPEN_READ;
    op.fd = -1;
    op.status = 0xFF;
    failures += check(fs_open_ex(&op) == BCOS_OK && op.fd >= 0, "open-ex-read");
    if(op.fd >= 0) {
        fs_close(op.fd);
    }

    op.path = "NOFILE.TXT";
    op.mode = BCOS_FS_OPEN_READ;
    op.fd = 1;
    op.status = 0;
    failures += check(fs_open_ex(&op) == BCOS_ERR_NOT_FOUND && op.fd < 0, "open-ex-missing");

    op.path = "BAD*NAME.TXT";
    op.mode = BCOS_FS_OPEN_READ;
    op.fd = 1;
    op.status = 0;
    failures += check(fs_open_ex(&op) == BCOS_ERR_BAD_NAME && op.fd < 0, "open-ex-bad-name");

    op.path = "README.TXT";
    op.mode = 0x7F;
    op.fd = 1;
    op.status = 0;
    failures += check(fs_open_ex(&op) == BCOS_ERR_GENERIC && op.fd < 0, "open-ex-bad-mode");
    failures += check(fs_open_ex(0) == BCOS_ERR_GENERIC, "open-ex-null");

    cwd.buffer = (char*)buf;
    cwd.length = 2;
    cwd.status = 0;
    failures += check(fs_getcwd(&cwd) == BCOS_ERR_GENERIC, "getcwd-small");
    failures += check(fs_getcwd(0) == BCOS_ERR_GENERIC, "getcwd-null");

    failures += check(fs_chdir("NOFOLDER") != BCOS_OK, "chdir-missing");
    failures += check(fs_chdir("BAD*NAME") != BCOS_OK, "chdir-bad-name");
    failures += check(fs_open("BAD*NAME.TXT") < 0, "open-bad-name");
    failures += check(fs_read_byte((fs_fd_t)-1) < 0, "read-byte-bad-fd");

    rw.fd = (fs_fd_t)-1;
    rw.buffer = buf;
    rw.length = 1;
    rw.actual = 123;
    rw.status = 0;
    failures += check(fs_read_block(&rw) == BCOS_ERR_EOF && rw.actual == 0, "read-block-bad-fd");
    failures += check(fs_read_block(0) == BCOS_ERR_GENERIC, "read-block-null");

    st.path = "BAD*NAME.TXT";
    st.status = 0;
    failures += check(fs_stat(&st) == BCOS_ERR_BAD_NAME, "stat-bad-name");
    failures += check(fs_stat(0) == BCOS_ERR_GENERIC, "stat-null");

    failures += check(fs_readdir(0) == BCOS_ERR_GENERIC, "readdir-null");
    fs_close((fs_fd_t)-1);
    failures += check(1, "close-bad-fd");

    for(i = 0; i < 4; i++) {
        buf[0] = 'D';
        buf[1] = 'G';
        buf[2] = '0' + (i / 10);
        buf[3] = '0' + (i % 10);
        buf[4] = '.';
        buf[5] = 'T';
        buf[6] = 'X';
        buf[7] = 'T';
        buf[8] = 0;
        fd = fs_create((const char*)buf);
        if(fd < 0) {
            break;
        }
        fs_close(fd);
    }
    failures += check(i == 4, "create-multiple");

    fd = fs_create("BAD*NAME.TXT");
    failures += check(fd < 0, "create-bad-name");
    if(fd >= 0) {
        fs_close(fd);
    }

    fd = fs_create("WR600.TXT");
    failures += check(fd >= 0, "create-write");
    if(fd >= 0) {
        for(j = 0; j < sizeof(bigbuf); j++) {
            bigbuf[j] = expected_write_byte(j);
        }
        rw.fd = fd;
        rw.buffer = bigbuf;
        rw.length = 520;
        rw.actual = 0;
        rw.status = 0xFF;
        failures += check(fs_write_block(&rw) == BCOS_OK && rw.actual == 520, "write-first");
        rw.buffer = bigbuf + 520;
        rw.length = 80;
        rw.actual = 0;
        rw.status = 0xFF;
        failures += check(fs_write_block(&rw) == BCOS_OK && rw.actual == 80, "write-second");
        failures += check(fs_flush(fd) == BCOS_OK, "write-flush");
        fs_close(fd);
    }

    fd = fs_create("WR600.TXT");
    failures += check(fd < 0, "create-duplicate");
    if(fd >= 0) {
        fs_close(fd);
    }

    failures += check(fs_flush((fs_fd_t)-1) == BCOS_ERR_BAD_FD, "flush-bad-fd");

    fd = fs_open("README.TXT");
    failures += check(fd >= 0, "open-readonly-for-write");
    if(fd >= 0) {
        rw.fd = fd;
        rw.buffer = bigbuf;
        rw.length = 1;
        rw.actual = 0;
        rw.status = 0;
        failures += check(fs_write_block(&rw) != BCOS_OK && rw.actual == 0, "write-readonly");
        failures += check(fs_flush(fd) == BCOS_OK, "flush-readonly");
        fs_close(fd);
    }
    failures += check(fs_write_block(0) == BCOS_ERR_GENERIC, "write-block-null");

    op.path = "OPENEX.TXT";
    op.mode = BCOS_FS_OPEN_CREATE;
    op.fd = -1;
    op.status = 0xFF;
    failures += check(fs_open_ex(&op) == BCOS_OK && op.fd >= 0, "open-ex-create");
    if(op.fd >= 0) {
        rw.fd = op.fd;
        rw.buffer = bigbuf;
        rw.length = 13;
        rw.actual = 0;
        rw.status = 0xFF;
        failures += check(fs_write_block(&rw) == BCOS_OK && rw.actual == 13, "open-ex-create-write");
        failures += check(fs_flush(op.fd) == BCOS_OK, "open-ex-create-flush");
        fs_close(op.fd);
    }

    st.path = "WR600.TXT";
    st.status = 0xFF;
    failures += check(fs_stat(&st) == BCOS_OK && st.size == 600, "stat-written");

    fd = fs_open("WR600.TXT");
    failures += check(fd >= 0, "open-written");
    if(fd >= 0) {
        rw.fd = fd;
        rw.buffer = bigbuf;
        rw.length = 600;
        rw.actual = 0;
        rw.status = 0xFF;
        failures += check(fs_read_block(&rw) == BCOS_OK && rw.actual == 600, "read-written");
        failures += check(bigbuf[0] == expected_write_byte(0) &&
                          bigbuf[511] == expected_write_byte(511) &&
                          bigbuf[512] == expected_write_byte(512) &&
                          bigbuf[599] == expected_write_byte(599), "verify-written");
        fs_close(fd);
    }

    st.path = "README.TXT";
    st.status = 0xFF;
    failures += check(fs_stat(&st) == BCOS_OK, "stat-readme");
    failures += check((st.attrib & BCOS_FS_ATTR_DIR) == 0 && st.size > 40, "stat-readme-data");

    st.path = "PROGRAMS";
    st.status = 0xFF;
    failures += check(fs_stat(&st) == BCOS_OK, "stat-programs");
    failures += check((st.attrib & BCOS_FS_ATTR_DIR) != 0, "stat-programs-dir");

    st.path = "NOFILE.TXT";
    st.status = 0;
    failures += check(fs_stat(&st) == BCOS_ERR_NOT_FOUND, "stat-missing");

    de.index = 0;
    failures += check(fs_readdir(&de) == BCOS_OK, "readdir-zero");
    failures += check(de.name[0] != 0 && de.cluster >= 2, "readdir-zero-data");

    de.index = 250;
    failures += check(fs_readdir(&de) == BCOS_ERR_EOF, "readdir-eof");

    failures += check(fs_chdir("PROGRAMS") == 0, "restore-programs");
    cwd.buffer = (char*)buf;
    cwd.length = sizeof(buf);
    cwd.status = 0xFF;
    failures += check(fs_getcwd(&cwd) == BCOS_OK &&
                      memcmp(buf, ":/PROGRAMS/> ", 13) == 0, "getcwd-programs");

    putstrnl(failures == 0 ? "FSTEST PASS" : "FSTEST FAIL");
    return failures == 0 ? 0 : 1;
}
