#include "fat32_internal.h"
#include "io.h"

struct FAT32Partition fat32_partition;
uint32_t fat32_linkedlist[F32_LLSZ];
struct FAT32Folder fat32_current_folder;
struct FAT32Folder fat32_root_folder;
struct FAT32File *fat32_files = (struct FAT32File*)FAT32FILESLOC;
struct FAT32Folder *fat32_fullpath = (struct FAT32Folder*)FAT32FOLDERLOC;
uint8_t fat32_pathdepth = 0;

struct FSOpenFile fs_open_files[FS_MAX_OPEN_FILES];
uint8_t fs_dir_sorted = 0;
uint8_t fs_dir_overflow = 0;
uint8_t fat32_chain_overflow = 0;
uint32_t fs_last_created_dir_lba = 0;
uint8_t fs_last_created_dir_slot = 0xFF;

static uint8_t fs_bank_enter(uint8_t bank) {
    uint8_t prev = get_rombank();
    set_rombank(bank);
    return prev;
}

static void fs_bank_exit(uint8_t prev) {
    set_rombank(prev);
}

uint8_t fs_mount(void) {
    uint8_t prev;
    uint8_t ret;
    prev = fs_bank_enter(SD_ROM_BANK);
    ret = fs_mount_impl();
    fs_bank_exit(prev);
    return ret;
}

uint8_t fs_chdir(const char* path) {
    uint8_t prev;
    uint8_t ret;
    prev = fs_bank_enter(SD_ROM_BANK);
    ret = fs_chdir_impl(path);
    fs_bank_exit(prev);
    return ret;
}

uint8_t fs_list_dir(void) {
    uint8_t prev;
    uint8_t ret;
    prev = fs_bank_enter(SD_ROM_BANK);
    ret = fs_list_dir_impl();
    fs_bank_exit(prev);
    return ret;
}

uint8_t fs_getcwd(char* out, uint8_t out_sz) {
    uint8_t prev;
    uint8_t ret;
    prev = fs_bank_enter(SD_ROM_BANK);
    ret = fs_getcwd_impl(out, out_sz);
    fs_bank_exit(prev);
    return ret;
}

uint8_t fs_getcwd_user(struct BCOSFsGetcwd* req) {
    uint8_t prev;

    if(req == NULL || req->buffer == NULL) {
        return 0xFF;
    }
    prev = fs_bank_enter(SD_ROM_BANK);
    req->status = fs_getcwd_impl(req->buffer, req->length);
    fs_bank_exit(prev);
    return req->status;
}

uint8_t fs_load_file(const char* filename, uint8_t* location, uint32_t* filesize_out) {
    uint8_t prev;
    uint8_t ret;
    prev = fs_bank_enter(SD_ROM_BANK);
    ret = fs_load_file_impl(filename, location, filesize_out);
    fs_bank_exit(prev);
    return ret;
}

uint8_t fs_mkdir(const char* name) {
    uint8_t prev;
    uint8_t ret;
    prev = fs_bank_enter(FS_WRITE_ROM_BANK);
    ret = fs_mkdir_impl(name);
    fs_bank_exit(prev);
    return ret;
}

uint8_t fs_touch(const char* filename) {
    uint8_t prev;
    uint8_t ret;
    prev = fs_bank_enter(FS_WRITE_ROM_BANK);
    ret = fs_touch_impl(filename);
    fs_bank_exit(prev);
    return ret;
}

fs_fd_t fs_open(const char* filename) {
    uint8_t prev;
    fs_fd_t ret;
    prev = fs_bank_enter(SD_ROM_BANK);
    ret = fs_open_impl(filename);
    fs_bank_exit(prev);
    return ret;
}

uint8_t fs_open_ex(struct BCOSFsOpen* req) {
    uint8_t prev;

    if(req == NULL || req->path == NULL) {
        return 0xFF;
    }
    req->fd = -1;
    if(req->mode != BCOS_FS_OPEN_READ && req->mode != BCOS_FS_OPEN_CREATE) {
        req->status = 0xFF;
        return req->status;
    }
    if(req->mode == BCOS_FS_OPEN_READ) {
        char name83[12];
        if(fs_make_83_filename(req->path, name83) != 0x00) {
            req->status = 0xFC;
            return req->status;
        }
        prev = fs_bank_enter(SD_ROM_BANK);
        req->fd = fs_open_impl(req->path);
        fs_bank_exit(prev);
        req->status = (req->fd >= 0) ? 0x00 : 0xFE;
        return req->status;
    }

    prev = fs_bank_enter(FS_WRITE_ROM_BANK);
    req->fd = fs_create_impl(req->path);
    fs_bank_exit(prev);
    req->status = (req->fd >= 0) ? 0x00 : 0xFF;
    return req->status;
}

uint16_t fs_read(fs_fd_t fd, uint8_t* dst, uint16_t len) {
    uint8_t prev;
    uint16_t ret;
    prev = fs_bank_enter(SD_ROM_BANK);
    ret = fs_read_impl(fd, dst, len);
    fs_bank_exit(prev);
    return ret;
}

int16_t fs_read_byte(fs_fd_t fd) {
    uint8_t prev;
    uint8_t byte = 0;
    uint16_t ret;
    prev = fs_bank_enter(SD_ROM_BANK);
    ret = fs_read_impl(fd, &byte, 1);
    fs_bank_exit(prev);
    return (ret == 1) ? (int16_t)byte : -1;
}

uint8_t fs_read_block(struct BCOSFsRW* req) {
    uint8_t prev;

    if(req == NULL || req->buffer == NULL) {
        return 0xFF;
    }
    prev = fs_bank_enter(SD_ROM_BANK);
    req->actual = fs_read_impl(req->fd, req->buffer, req->length);
    fs_bank_exit(prev);

    if(req->actual != 0) {
        req->status = 0x00;
    } else {
        req->status = 0xFB;
    }
    return req->status;
}

fs_fd_t fs_create(const char* filename) {
    uint8_t prev;
    fs_fd_t ret;
    prev = fs_bank_enter(FS_WRITE_ROM_BANK);
    ret = fs_create_impl(filename);
    fs_bank_exit(prev);
    return ret;
}

uint8_t fs_write_block(struct BCOSFsRW* req) {
    uint8_t prev;

    if(req == NULL || req->buffer == NULL) {
        return 0xFF;
    }
    prev = fs_bank_enter(FS_WRITE_ROM_BANK);
    req->actual = fs_write_impl(req->fd, req->buffer, req->length);
    fs_bank_exit(prev);

    req->status = (req->actual == req->length) ? 0x00 : 0xFF;
    return req->status;
}

uint8_t fs_flush(fs_fd_t fd) {
    uint8_t prev;
    uint8_t ret;

    if(fd < 0 || fd >= FS_MAX_OPEN_FILES || !fs_open_files[fd].in_use) {
        return 0xFA;
    }
    if(fs_open_files[fd].mode != FS_MODE_WRITE) {
        return 0x00;
    }
    prev = fs_bank_enter(FS_WRITE_ROM_BANK);
    ret = fs_flush_impl(fd);
    fs_bank_exit(prev);
    return ret;
}

void fs_close(fs_fd_t fd) {
    uint8_t prev;
    if(fd >= 0 && fd < FS_MAX_OPEN_FILES &&
       fs_open_files[fd].in_use &&
       fs_open_files[fd].mode == FS_MODE_WRITE &&
       fs_open_files[fd].file.filesize != 0) {
        fs_flush(fd);
    }
    prev = fs_bank_enter(SD_ROM_BANK);
    fs_close_impl(fd);
    fs_bank_exit(prev);
}

uint8_t fs_stat(struct BCOSFsStat* req) {
    uint8_t prev;
    uint8_t ret;

    if(req == NULL) {
        return 0xFF;
    }
    prev = fs_bank_enter(SD_ROM_BANK);
    ret = fs_stat_impl(req);
    fs_bank_exit(prev);
    return ret;
}

uint8_t fs_readdir(struct BCOSFsDirent* req) {
    uint8_t prev;
    uint8_t ret;

    if(req == NULL) {
        return 0xFF;
    }
    prev = fs_bank_enter(SD_ROM_BANK);
    ret = fs_readdir_impl(req);
    fs_bank_exit(prev);
    return ret;
}

void fs_print_partition_info(void) {
    uint8_t prev;
    prev = fs_bank_enter(SD_ROM_BANK);
    fs_print_partition_info_impl();
    fs_bank_exit(prev);
}
