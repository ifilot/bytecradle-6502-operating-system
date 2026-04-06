#include "fat32.h"
#include "io.h"
#include "constants.h"

#define SD_ROM_BANK 2
#define FS_MAX_OPEN_FILES 4
#define FS_SD_BOOT_RETRIES 10
#define FS_MAX_PATH_DEPTH 16

static struct FAT32Partition fat32_partition;
static uint32_t fat32_linkedlist[F32_LLSZ];
static struct FAT32Folder fat32_current_folder;
static struct FAT32Folder fat32_root_folder;
static struct FAT32File *fat32_files = (struct FAT32File*)FAT32FILESLOC;
static struct FAT32Folder *fat32_fullpath = (struct FAT32Folder*)FAT32FOLDERLOC;
static uint8_t fat32_pathdepth = 0;

struct FSOpenFile {
    uint8_t in_use;
    struct FAT32File file;
    uint32_t offset;
    uint32_t clusters[F32_LLSZ];
    uint8_t cluster_count;
    uint8_t cluster_overflow;
};

static struct FSOpenFile fs_open_files[FS_MAX_OPEN_FILES];
static uint8_t fs_dir_sorted = 0;
static uint8_t fs_dir_overflow = 0;
static uint8_t fat32_chain_overflow = 0;

static uint8_t fs_mount_impl(void);
static uint8_t fs_chdir_impl(const char* path);
static uint8_t fs_list_dir_impl(void);
static uint8_t fs_getcwd_impl(char* out, uint8_t out_sz);
static uint8_t fs_load_file_impl(const char* filename, uint8_t* location, uint32_t* filesize_out);
static uint8_t fs_mkdir_impl(const char* name);
static uint8_t fs_touch_impl(const char* filename);
static fs_fd_t fs_open_impl(const char* filename);
static uint16_t fs_read_impl(fs_fd_t fd, uint8_t* dst, uint16_t len);
static void fs_close_impl(fs_fd_t fd);
static void fs_print_partition_info_impl(void);

static uint8_t fs_fat32_read_mbr(void);
static uint8_t fs_fat32_read_partition(void);
static void fs_fat32_print_partition_info(void);
static uint8_t fs_fat32_read_dir(void);
static void fs_fat32_sort_files(void);
static void fs_fat32_list_dir(void);
static uint8_t fs_fat32_wait_for_listing_keypress(void);
static struct FAT32File* fs_fat32_search_dir(const char* filename, uint8_t entry_type);
static uint8_t fs_fat32_load_file(const struct FAT32File* fileptr, uint8_t* location);
static uint8_t fs_fat32_build_linked_list(uint32_t nextcluster);
static uint32_t fs_fat32_grab_cluster_address_from_fileblock(uint8_t *loc);
static uint32_t fs_fat32_calculate_sector_address(uint32_t cluster, uint8_t sector);
static int fs_fat32_file_compare(const void* item1, const void *item2);
static uint8_t fs_fat32_find_free_cluster(uint32_t* cluster_out);
static uint8_t fs_fat32_set_fat_entry(uint32_t cluster, uint32_t value);
static uint8_t fs_fat32_mirror_fat_sector(uint32_t fat_sector_lba);

/**
 * @brief Switch to BANK2 and return previous ROM bank.
 *
 * @return Previously active bank id.
 */
static uint8_t fs_bank_enter(void) {
    uint8_t prev = get_rombank();
    set_rombank(SD_ROM_BANK);
    return prev;
}

/**
 * @brief Restore the previously active ROM bank.
 *
 * @param prev ROM bank id to restore.
 */
static void fs_bank_exit(uint8_t prev) {
    set_rombank(prev);
}

/**
 * @brief Mount filesystem (public BANK2 wrapper).
 *
 * @return 0 on success, 0xFF on error.
 */
uint8_t fs_mount(void) {
    uint8_t prev = fs_bank_enter();
    uint8_t ret = fs_mount_impl();
    fs_bank_exit(prev);
    return ret;
}

/**
 * @brief Change current working directory (public BANK2 wrapper).
 *
 * @param path Directory path.
 * @return 0 on success, 0xFF on error.
 */
uint8_t fs_chdir(const char* path) {
    uint8_t prev = fs_bank_enter();
    uint8_t ret = fs_chdir_impl(path);
    fs_bank_exit(prev);
    return ret;
}

/**
 * @brief List current working directory (public BANK2 wrapper).
 *
 * @return 0 on success, 0xFF on error.
 */
uint8_t fs_list_dir(void) {
    uint8_t prev = fs_bank_enter();
    uint8_t ret = fs_list_dir_impl();
    fs_bank_exit(prev);
    return ret;
}

/**
 * @brief Retrieve current working directory (public BANK2 wrapper).
 *
 * @param out Destination buffer.
 * @param out_sz Destination buffer size.
 * @return 0 on success, 0xFF on error.
 */
uint8_t fs_getcwd(char* out, uint8_t out_sz) {
    uint8_t prev = fs_bank_enter();
    uint8_t ret = fs_getcwd_impl(out, out_sz);
    fs_bank_exit(prev);
    return ret;
}

/**
 * @brief Load file contents into memory (public BANK2 wrapper).
 *
 * @param filename 8.3 filename.
 * @param location Destination memory address.
 * @param filesize_out Optional file size output.
 * @return 0 on success, 0xFF on error.
 */
uint8_t fs_load_file(const char* filename, uint8_t* location, uint32_t* filesize_out) {
    uint8_t prev = fs_bank_enter();
    uint8_t ret = fs_load_file_impl(filename, location, filesize_out);
    fs_bank_exit(prev);
    return ret;
}

/**
 * @brief Create child folder (public BANK2 wrapper).
 *
 * @param name Folder name (8.3 compatible, no extension).
 * @return 0 on success, 0xFF on error.
 */
uint8_t fs_mkdir(const char* name) {
    uint8_t prev = fs_bank_enter();
    uint8_t ret = fs_mkdir_impl(name);
    fs_bank_exit(prev);
    return ret;
}

/**
 * @brief Create empty file entry (public BANK2 wrapper).
 *
 * @param filename 8.3 filename.
 * @return 0 on success, 0xFF on error.
 */
uint8_t fs_touch(const char* filename) {
    uint8_t prev = fs_bank_enter();
    uint8_t ret = fs_touch_impl(filename);
    fs_bank_exit(prev);
    return ret;
}

/**
 * @brief Open file descriptor (public BANK2 wrapper).
 *
 * @param filename 8.3 filename.
 * @return File descriptor [0..3] or -1 on error.
 */
fs_fd_t fs_open(const char* filename) {
    uint8_t prev = fs_bank_enter();
    fs_fd_t ret = fs_open_impl(filename);
    fs_bank_exit(prev);
    return ret;
}

/**
 * @brief Read bytes from an open descriptor (public BANK2 wrapper).
 *
 * @param fd File descriptor.
 * @param dst Destination buffer.
 * @param len Number of bytes requested.
 * @return Number of bytes read.
 */
uint16_t fs_read(fs_fd_t fd, uint8_t* dst, uint16_t len) {
    uint8_t prev = fs_bank_enter();
    uint16_t ret = fs_read_impl(fd, dst, len);
    fs_bank_exit(prev);
    return ret;
}

/**
 * @brief Close file descriptor (public BANK2 wrapper).
 *
 * @param fd File descriptor.
 */
void fs_close(fs_fd_t fd) {
    uint8_t prev = fs_bank_enter();
    fs_close_impl(fd);
    fs_bank_exit(prev);
}

/**
 * @brief Print FAT32 partition information (public BANK2 wrapper).
 */
void fs_print_partition_info(void) {
    uint8_t prev = fs_bank_enter();
    fs_print_partition_info_impl();
    fs_bank_exit(prev);
}

#pragma code-name(push, "B2CODE")
#pragma rodata-name(push, "B2RODATA")

/**
 * @brief Convert filename to upper-case FAT 8.3 representation.
 *
 * @param filename Input filename.
 * @param out Destination 11-byte name buffer.
 * @return 0 on success, 0xFF on invalid input.
 */
static uint8_t fs_make_83_filename(const char* filename, char* out) {
    const char *dot;
    const char *p;
    uint8_t i = 0;
    uint8_t j = 0;

    if(filename == NULL || out == NULL || *filename == '\0') {
        return 0xFF;
    }

    dot = strchr(filename, '.');
    if(dot != NULL && strchr(dot + 1, '.') != NULL) {
        return 0xFF;
    }

    for(p = filename; *p != '\0'; p++) {
        char c = *p;
        if(c == '.') {
            continue;
        }
        if(!((c >= '0' && c <= '9') ||
             (c >= 'A' && c <= 'Z') ||
             (c >= 'a' && c <= 'z') ||
             c == '_' || c == '-' || c == '$' || c == '~')) {
            return 0xFF;
        }
    }

    memset(out, ' ', 11);

    while(filename[i] != '\0' && filename[i] != '.' && i < 8) {
        char c = filename[i];
        if(c >= 'a' && c <= 'z') {
            c -= 32;
        }
        out[i] = c;
        i++;
    }
    if(filename[i] != '\0' && filename[i] != '.') {
        return 0xFF;
    }

    if(dot != NULL) {
        dot++;
        while(dot[j] != '\0' && j < 3) {
            char c = dot[j];
            if(c >= 'a' && c <= 'z') {
                c -= 32;
            }
            out[8 + j] = c;
            j++;
        }
        if(dot[j] != '\0') {
            return 0xFF;
        }
    }

    return 0;
}

/**
 * @brief Cache cluster chain for an open file handle.
 *
 * @param handle Open file handle.
 * @return 0 on success, 0xFF on error.
 */
static uint8_t fs_build_cluster_cache(struct FSOpenFile* handle) {
    uint8_t i;

    if(handle == NULL) {
        return 0xFF;
    }

    if(fs_fat32_build_linked_list(handle->file.cluster) != 0x00) {
        return 0xFF;
    }
    handle->cluster_count = 0;
    handle->cluster_overflow = fat32_chain_overflow;
    for(i=0; i<F32_LLSZ; i++) {
        if(fat32_linkedlist[i] == 0xFFFFFFFF) {
            break;
        }
        handle->clusters[i] = fat32_linkedlist[i];
        handle->cluster_count++;
    }

    return (handle->cluster_count == 0) ? 0xFF : 0;
}

/**
 * @brief BANK2 filesystem mount implementation.
 *
 * @return 0 on success, 0xFF on failure.
 */
static uint8_t fs_mount_impl(void) {
    uint8_t i;

    memset(fs_open_files, 0x00, sizeof(fs_open_files));

    for(i=0; i<FS_SD_BOOT_RETRIES; i++) {
        if(boot_sd() == 0x00) {
            break;
        }
    }

    if(i == FS_SD_BOOT_RETRIES) {
        return 0xFF;
    }

    if(fs_fat32_read_mbr() != 0x00) {
        return 0xFF;
    }

    if(fs_fat32_read_partition() != 0x00) {
        return 0xFF;
    }

    return 0;
}

/**
 * @brief BANK2 implementation for directory change.
 *
 * @param path Directory path.
 * @return 0 on success, 0xFF on failure.
 */
static uint8_t fs_chdir_impl(const char* path) {
    struct FAT32File* res = NULL;
    char dirname[12];

    if(path == NULL) {
        return 0xFF;
    }

    if(strcmp(path, "/") == 0 || *path == '\0') {
        fat32_current_folder = fat32_root_folder;
        fat32_pathdepth = 1;
        return 0;
    }

    if(strcmp(path, "..") == 0) {
        if(fat32_pathdepth > 1) {
            fat32_pathdepth--;
            fat32_current_folder = fat32_fullpath[fat32_pathdepth - 1];
        }
        return 0;
    }

    if(fs_make_83_filename(path, dirname) != 0x00) {
        return 0xFF;
    }
    dirname[11] = 0x00;

    if(fs_fat32_read_dir() != 0x00) {
        return 0xFF;
    }
    res = fs_fat32_search_dir(dirname, FOLDER_ENTRY);
    if(res == NULL) {
        return 0xFF;
    }

    if(res->cluster == 0) {
        fat32_current_folder = fat32_root_folder;
        fat32_pathdepth = 1;
        return 0;
    }

    memcpy(fat32_current_folder.name, res->basename, 11);
    fat32_current_folder.cluster = res->cluster;
    fat32_current_folder.nrfiles = 0;
    if(fat32_pathdepth >= FS_MAX_PATH_DEPTH) {
        return 0xFF;
    }
    fat32_fullpath[fat32_pathdepth++] = fat32_current_folder;
    return 0;
}

/**
 * @brief BANK2 implementation for directory listing.
 *
 * @return 0 on success.
 */
static uint8_t fs_list_dir_impl(void) {
    if(fs_fat32_read_dir() != 0x00) {
        return 0xFF;
    }
    fs_fat32_sort_files();
    fs_fat32_list_dir();
    if(fs_dir_overflow) {
        putstrnl("Warning: directory truncated; too many entries.");
    }
    return 0;
}

/**
 * @brief BANK2 implementation for getcwd.
 *
 * @param out Destination buffer.
 * @param out_sz Destination size.
 * @return 0 on success, 0xFF on overflow/error.
 */
static uint8_t fs_getcwd_impl(char* out, uint8_t out_sz) {
    char *ptr = out;
    uint8_t i;
    uint8_t n = 0;
    const char *nameptr;

    if(out == NULL || out_sz < 4) {
        return 0xFF;
    }

    *(ptr++) = ':';
    n++;
    for(i=0; i<fat32_pathdepth; i++) {
        nameptr = fat32_fullpath[i].name;
        while(*nameptr != 0x00 && *nameptr != ' ') {
            if(n >= (out_sz - 2)) {
                return 0xFF;
            }
            *(ptr++) = *(nameptr++);
            n++;
        }
        if(n >= (out_sz - 2)) {
            return 0xFF;
        }
        *(ptr++) = '/';
        n++;
    }
    *(ptr++) = '>';
    *(ptr++) = ' ';
    *ptr = 0x00;
    return 0;
}

/**
 * @brief BANK2 implementation for whole-file load.
 *
 * @param filename 8.3 filename.
 * @param location Destination memory.
 * @param filesize_out Optional output size pointer.
 * @return 0 on success, 0xFF on failure.
 */
static uint8_t fs_load_file_impl(const char* filename, uint8_t* location, uint32_t* filesize_out) {
    struct FAT32File* res;
    char filename83[12];

    if(filename == NULL || location == NULL) {
        return 0xFF;
    }

    if(fs_make_83_filename(filename, filename83) != 0x00) {
        return 0xFF;
    }
    filename83[11] = 0x00;

    if(fs_fat32_read_dir() != 0x00) {
        return 0xFF;
    }
    res = fs_fat32_search_dir(filename83, FILE_ENTRY);
    if(res == NULL) {
        return 0xFF;
    }

    if(res->filesize >= 0x7500) {
        return 0xFF;
    }

    if(fs_fat32_load_file(res, location) != 0x00) {
        return 0xFF;
    }
    if(filesize_out != NULL) {
        *filesize_out = res->filesize;
    }
    return 0;
}


/**
 * @brief BANK2 implementation for mkdir.
 */
static uint8_t fs_mkdir_impl(const char* name) {
    char name83[12];
    uint32_t new_cluster;
    uint32_t dir_lba;
    uint8_t i;
    uint8_t dir_slot = 0xFF;

    if(name == NULL) {
        return 0xFF;
    }

    if(fs_make_83_filename(name, name83) != 0x00) {
        return 0xFF;
    }
    if(name83[8] != ' ') {
        return 0xFF;
    }
    name83[11] = 0x00;

    if(fs_fat32_find_free_cluster(&new_cluster) != 0x00) {
        return 0xFF;
    }
    if(fs_fat32_set_fat_entry(new_cluster, 0x0FFFFFFF) != 0x00) {
        return 0xFF;
    }

    for(i=0; i<fat32_partition.sectors_per_cluster; i++) {
        memset((uint8_t*)SDBUF, 0x00, 512);
        if(i == 0) {
            memset((uint8_t*)SDBUF, ' ', 11);
            *((uint8_t*)SDBUF) = '.';
            *((uint8_t*)SDBUF + 0x0B) = MASK_DIR;
            *(uint16_t*)(SDBUF + 0x14) = (uint16_t)(new_cluster >> 16);
            *(uint16_t*)(SDBUF + 0x1A) = (uint16_t)(new_cluster & 0xFFFF);

            memset((uint8_t*)SDBUF + 32, ' ', 11);
            *((uint8_t*)SDBUF + 32) = '.';
            *((uint8_t*)SDBUF + 33) = '.';
            *((uint8_t*)SDBUF + 32 + 0x0B) = MASK_DIR;
            *(uint16_t*)(SDBUF + 32 + 0x14) = (uint16_t)(fat32_current_folder.cluster >> 16);
            *(uint16_t*)(SDBUF + 32 + 0x1A) = (uint16_t)(fat32_current_folder.cluster & 0xFFFF);
        }
        if(write_sector(fs_fat32_calculate_sector_address(new_cluster, i)) != 0x00) {
            return 0xFF;
        }
    }

    dir_lba = fs_fat32_calculate_sector_address(fat32_current_folder.cluster, 0);
    if(read_sector(dir_lba) != 0x00) {
        return 0xFF;
    }
    for(i=0; i<16; i++) {
        uint8_t first = *((uint8_t*)SDBUF + ((uint16_t)i << 5));
        if(first == 0x00 || first == 0xE5) {
            dir_slot = i;
            break;
        }
    }
    if(dir_slot == 0xFF) {
        return 0xFF;
    }
    memset((uint8_t*)SDBUF + ((uint16_t)dir_slot << 5), 0x00, 32);
    memcpy((uint8_t*)SDBUF + ((uint16_t)dir_slot << 5), name83, 11);
    *((uint8_t*)SDBUF + ((uint16_t)dir_slot << 5) + 0x0B) = MASK_DIR;
    *(uint16_t*)(SDBUF + ((uint16_t)dir_slot << 5) + 0x14) = (uint16_t)(new_cluster >> 16);
    *(uint16_t*)(SDBUF + ((uint16_t)dir_slot << 5) + 0x1A) = (uint16_t)(new_cluster & 0xFFFF);
    if(write_sector(dir_lba) != 0x00) {
        return 0xFF;
    }

    return 0;
}

/**
 * @brief BANK2 implementation for touch.
 */
#pragma code-name(push, "CODE")
#pragma rodata-name(push, "RODATA")
static uint8_t fs_touch_impl(const char* filename) {
    char name83[12];
    uint32_t dir_lba;
    uint8_t dir_slot = 0xFF;
    uint8_t i;

    if(filename == NULL) {
        return 0xFF;
    }

    if(fs_make_83_filename(filename, name83) != 0x00) {
        return 0xFF;
    }
    name83[11] = 0x00;

    dir_lba = fs_fat32_calculate_sector_address(fat32_current_folder.cluster, 0);
    if(read_sector(dir_lba) != 0x00) {
        return 0xFF;
    }
    for(i=0; i<16; i++) {
        uint8_t first = *((uint8_t*)SDBUF + ((uint16_t)i << 5));
        if(first == 0x00 || first == 0xE5) {
            dir_slot = i;
            break;
        }
    }
    if(dir_slot == 0xFF) {
        return 0xFF;
    }
    memset((uint8_t*)SDBUF + ((uint16_t)dir_slot << 5), 0x00, 32);
    memcpy((uint8_t*)SDBUF + ((uint16_t)dir_slot << 5), name83, 11);
    *((uint8_t*)SDBUF + ((uint16_t)dir_slot << 5) + 0x0B) = 0x20;
    if(write_sector(dir_lba) != 0x00) {
        return 0xFF;
    }

    return 0;
}
#pragma rodata-name(pop)
#pragma code-name(pop)

/**
 * @brief BANK2 implementation for file-open.
 *
 * @param filename 8.3 filename.
 * @return File descriptor [0..3] or -1 on error.
 */
static fs_fd_t fs_open_impl(const char* filename) {
    struct FAT32File* res;
    char filename83[12];
    fs_fd_t fd;

    if(filename == NULL) {
        return -1;
    }

    if(fs_make_83_filename(filename, filename83) != 0x00) {
        return -1;
    }
    filename83[11] = 0x00;

    if(fs_fat32_read_dir() != 0x00) {
        return -1;
    }
    res = fs_fat32_search_dir(filename83, FILE_ENTRY);
    if(res == NULL) {
        return -1;
    }
    if(res->attrib & MASK_DIR) {
        return -1;
    }

    for(fd=0; fd<FS_MAX_OPEN_FILES; fd++) {
        if(!fs_open_files[fd].in_use) {
            fs_open_files[fd].in_use = 1;
            fs_open_files[fd].file = *res;
            fs_open_files[fd].offset = 0;
            if(fs_build_cluster_cache(&fs_open_files[fd]) != 0x00) {
                fs_open_files[fd].in_use = 0;
                return -1;
            }
            return fd;
        }
    }

    return -1;
}

/**
 * @brief BANK2 implementation for descriptor reads.
 *
 * @param fd File descriptor.
 * @param dst Destination buffer.
 * @param len Bytes requested.
 * @return Bytes actually read.
 */
static uint16_t fs_read_impl(fs_fd_t fd, uint8_t* dst, uint16_t len) {
    struct FSOpenFile *handle;
    uint16_t nread = 0;
    uint32_t remaining;
    uint32_t sector_in_file;
    uint32_t cluster_index;
    uint32_t lba;
    uint8_t sector_in_cluster;
    uint16_t copy_n;
    uint16_t sector_offset;

    if(fd < 0 || fd >= FS_MAX_OPEN_FILES || dst == NULL || len == 0) {
        return 0;
    }

    handle = &fs_open_files[fd];
    if(!handle->in_use) {
        return 0;
    }
    if(handle->cluster_overflow) {
        return 0;
    }

    if(handle->offset >= handle->file.filesize || fat32_partition.sectors_per_cluster == 0) {
        return 0;
    }

    remaining = handle->file.filesize - handle->offset;
    if(remaining > len) {
        remaining = len;
    }

    while(remaining > 0) {
        sector_in_file = handle->offset >> 9;             // / 512
        sector_offset = (uint16_t)(handle->offset & 0x1FF);
        cluster_index = sector_in_file / fat32_partition.sectors_per_cluster;
        sector_in_cluster = sector_in_file % fat32_partition.sectors_per_cluster;

        if(cluster_index >= handle->cluster_count) {
            break;
        }

        lba = fs_fat32_calculate_sector_address(handle->clusters[cluster_index], sector_in_cluster);
        if(read_sector(lba) != 0x00) {
            break;
        }

        copy_n = 0x200 - sector_offset;
        if(copy_n > remaining) {
            copy_n = remaining;
        }

        memcpy(dst + nread, (uint8_t*)SDBUF + sector_offset, copy_n);
        nread += copy_n;
        handle->offset += copy_n;
        remaining -= copy_n;
    }

    return nread;
}

/**
 * @brief BANK2 implementation for descriptor close.
 *
 * @param fd File descriptor.
 */
static void fs_close_impl(fs_fd_t fd) {
    if(fd < 0 || fd >= FS_MAX_OPEN_FILES) {
        return;
    }
    fs_open_files[fd].in_use = 0;
    fs_open_files[fd].offset = 0;
    fs_open_files[fd].cluster_count = 0;
    fs_open_files[fd].cluster_overflow = 0;
}

/**
 * @brief BANK2 implementation for partition-info printing.
 */
static void fs_print_partition_info_impl(void) {
    fs_fat32_print_partition_info();
}

/**
 * Read the Master Boot Record from the SD card, assumes that the SD-card
 * has already been initialized.
 */
static uint8_t fs_fat32_read_mbr(void) {
    uint8_t* sdbuf = (uint8_t*)(SDBUF);

    if(read_sector(0x00000000) != 0x00) {
        return 0xFF;
    }

    // check partition type
    if(sdbuf[0x1C2] != 0x0C && sdbuf[0x1C2] != 0x0B) {
        return -1;
    }

    // check pattern
    if(sdbuf[0x1FE] != 0x55 || sdbuf[0x1FF] != 0xAA) {
        return -1;
    }

    return 0;
}

/**
 * Read the partition table from the MBR
 * 
 * This function assumes that the MBR has already been read and it present
 * in RAM at location 0x8000
 */
static uint8_t fs_fat32_read_partition(void) {
    // extract logical block address (LBA) from partition table
    uint32_t lba = *(uint32_t*)(SDBUF + 0x1C6);

    // retrieve the partition table
    if(read_sector(lba) != 0x00) {
        return 0xFF;
    }

    // store partition information
    fat32_partition.bytes_per_sector = *(uint16_t*)(SDBUF + 0x0B);
    fat32_partition.sectors_per_cluster = *(uint8_t*)(SDBUF + 0x0D);
    fat32_partition.reserved_sectors = *(uint16_t*)(SDBUF + 0x0E);
    fat32_partition.number_of_fats = *(uint8_t*)(SDBUF + 0x10);
    fat32_partition.sectors_per_fat = *(uint32_t*)(SDBUF + 0x24);
    fat32_partition.root_dir_first_cluster = *(uint32_t*)(SDBUF + 0x2C);
    fat32_partition.fat_begin_lba = lba + fat32_partition.reserved_sectors;
    fat32_partition.fat2_begin_lba = fat32_partition.fat_begin_lba + fat32_partition.sectors_per_fat;
    fat32_partition.sector_begin_lba = fat32_partition.fat_begin_lba + 
        (fat32_partition.number_of_fats * fat32_partition.sectors_per_fat);
    fat32_partition.lba_addr_root_dir = fs_fat32_calculate_sector_address(fat32_partition.root_dir_first_cluster, 0);

    if(fat32_partition.bytes_per_sector != 512) {
        return 0xFF;
    }
    if(fat32_partition.sectors_per_cluster == 0 ||
       (fat32_partition.sectors_per_cluster & (fat32_partition.sectors_per_cluster - 1)) != 0) {
        return 0xFF;
    }
    if(fat32_partition.number_of_fats == 0 || fat32_partition.sectors_per_fat == 0) {
        return 0xFF;
    }
    if(fat32_partition.root_dir_first_cluster < 2) {
        return 0xFF;
    }

    // set the cluster of the current folder
    fat32_current_folder.cluster = fat32_partition.root_dir_first_cluster;
    fat32_current_folder.nrfiles = 0;
    memset(fat32_current_folder.name, 0x00, 11);
    fat32_root_folder = fat32_current_folder;
    fat32_fullpath[0] = fat32_root_folder;
    fat32_pathdepth = 1;

    // read first sector of first partition to establish volume name
    if(read_sector(fat32_partition.lba_addr_root_dir) != 0x00) {
        return 0xFF;
    }

    // copy volume name
    memcpy(fat32_partition.volume_label, (uint8_t*)(SDBUF), 11);
    return 0;
}

/**
 * Print partition info
 */
static void fs_fat32_print_partition_info() {
    char buf[32];
    sprintf(buf, "LBA partition 1: %08lX", fat32_partition.fat_begin_lba - 
                                           fat32_partition.reserved_sectors);
    putstrnl(buf);

    sprintf(buf, "Bytes per sector: %i", fat32_partition.bytes_per_sector);
    putstrnl(buf);

    sprintf(buf, "Sectors per cluster: %i", fat32_partition.sectors_per_cluster);
    putstrnl(buf);

    sprintf(buf, "Reserved sectors: %i", fat32_partition.reserved_sectors);
    putstrnl(buf);

    sprintf(buf, "Number of FATS: %i", fat32_partition.number_of_fats);
    putstrnl(buf);

    sprintf(buf, "Sectors per FAT: %lu", fat32_partition.sectors_per_fat);
    putstrnl(buf);

    sprintf(buf, "Partition size: %lu MiB", (fat32_partition.sectors_per_fat * 
                                             fat32_partition.sectors_per_cluster * 
                                             fat32_partition.bytes_per_sector) >> 13);
    putstrnl(buf);

    sprintf(buf, "Address ROOT dir: %08lX", fat32_partition.lba_addr_root_dir);
    putstrnl(buf);

    sprintf(buf, "Volume name: %.11s\0", (uint8_t*)(SDBUF));
    putstrnl(buf);
}

/**
 * Read the contents of the current directory, store the result starting
 * at 0x8200.
 */
static uint8_t fs_fat32_read_dir() {
    uint8_t ctr = 0;                // counter over clusters
    uint16_t fctr = 0;              // counter over directory entries (files and folders)
    uint8_t i = 0, j = 0;
    uint8_t* locptr = NULL;
    struct FAT32File *file = NULL;

    fs_dir_sorted = 0;
    fs_dir_overflow = 0;

    // build linked list
    if(fs_fat32_build_linked_list(fat32_current_folder.cluster) != 0x00) {
        return 0xFF;
    }

    while(fat32_linkedlist[ctr] != 0xFFFFFFFF && ctr < F32_LLSZ) {
        // print cluster number and address
        uint32_t caddr = fs_fat32_calculate_sector_address(fat32_linkedlist[ctr], 0);

        // loop over all sectors per cluster
        for(i=0; i<fat32_partition.sectors_per_cluster; i++) {
            if(read_sector(caddr) != 0x00) {
                return 0xFF;
            }
            locptr = (uint8_t*)(SDBUF);    // set pointer to sector data
            for(j=0; j<16; j++) { // 16 file tables per sector
                // continue if an unused entry is encountered 0xE5
                if(*locptr == 0xE5) {
                    locptr += 32;  // next file entry location
                    continue;
                }

                // early exit if a zero is read
                if(*locptr == 0x00) {
                    fat32_current_folder.nrfiles = fctr;
                    return 0;
                }

                // skip long-file-name entries
                if((*(locptr + 0x0B) & 0x0F) == 0x0F) {
                    locptr += 32;  // next file entry location
                    continue;
                }

                // check if we are reading a file or a folder
                if((*(locptr + 0x0B) & 0x08) == 0x00) {
                    if(fctr >= MAXFILES) {
                        fs_dir_overflow = 1;
                        putstrnl("Error: too many entries in folder; cannot parse...");
                        fat32_current_folder.nrfiles = fctr;
                        return 0xFF;
                    }
                    file = &fat32_files[fctr];
                    memcpy(file->basename, locptr, 11);
                    file->termbyte = 0x00;  // by definition
                    file->attrib = *(locptr + 0x0B);
                    file->cluster = fs_fat32_grab_cluster_address_from_fileblock(locptr);
                    file->filesize = *(uint32_t*)(locptr + 0x1C);
                    fctr++;
                }
                locptr += 32;  // next file entry location
            }
            caddr++;    // next sector
        }
        ctr++;  // next cluster
    }
    fat32_current_folder.nrfiles = fctr;
    return fat32_chain_overflow ? 0xFF : 0;
}

/**
 * Sort the files in the file list
 */
static void fs_fat32_sort_files() {
    qsort(fat32_files, fat32_current_folder.nrfiles, sizeof(struct FAT32File), fs_fat32_file_compare);
    fs_dir_sorted = 1;
}

/**
 * List the contents of the current directory
 */
static void fs_fat32_list_dir() {
    uint16_t i = 0;
    uint8_t buf[80];
    struct FAT32File *file = fat32_files;
    uint8_t lines_printed = 0;
    uint16_t nr_files = 0;
    uint32_t total_size = 0;

    for(i = 0; i<fat32_current_folder.nrfiles; i++) {
        if(file->attrib & MASK_DIR) {
            sprintf(buf, "%.8s %.3s %08lX DIR", file->basename, file->extension, file->cluster);
        } else {
            sprintf(buf, "%.8s.%.3s %08lX %lu", file->basename, file->extension, file->cluster, file->filesize);
            nr_files++;
            total_size += file->filesize;
        }
        putstrnl(buf);
        lines_printed++;
        if(lines_printed >= 24) {
            putstrnl("--- Press any key for next page, Q to quit ---");
            if(fs_fat32_wait_for_listing_keypress() != 0) {
                return;
            }
            lines_printed = 0;
        }
        file++;
    }

    sprintf(buf, "%u File(s) %lu bytes", nr_files, total_size);
    putstrnl(buf);
}

/**
 * Wait for a keypress while listing files.
 * Returns 1 when the user requested to quit.
 */
static uint8_t fs_fat32_wait_for_listing_keypress() {
    uint8_t c;
    do {
        c = getch();
    } while(c == 0);

    return (uint8_t)(c == 'q' || c == 'Q');
}

/**
 * Find a file
 */
static struct FAT32File* fs_fat32_search_dir(const char* filename, uint8_t entry_type) {
    struct FAT32File key;
    uint16_t i;
    struct FAT32File* file = fat32_files;
    memcpy(key.basename, filename, 11);
    key.termbyte = 0x00;
    key.attrib = (entry_type == FOLDER_ENTRY) ? MASK_DIR : 0;

    if(fs_dir_sorted) {
        return (struct FAT32File*)bsearch(&key, fat32_files, fat32_current_folder.nrfiles, sizeof(struct FAT32File), fs_fat32_file_compare);
    }

    for(i=0; i<fat32_current_folder.nrfiles; i++) {
        if((file->attrib & MASK_DIR) == key.attrib && strcmp(file->basename, key.basename) == 0) {
            return file;
        }
        file++;
    }

    return NULL;
 }

/**
 * @brief Calculate the sector address from cluster and sector
 * 
 * @param cluster which cluster
 * @param sector which sector on the cluster (0-Nclusters)
 * @return uint32_t sector address (512 byte address)
 */
static uint32_t fs_fat32_calculate_sector_address(uint32_t cluster, uint8_t sector) {
    return fat32_partition.sector_begin_lba + (cluster - 2) * fat32_partition.sectors_per_cluster + sector;   
}

/**
 * Load a file to location in RAM
 */
static uint8_t fs_fat32_load_file(const struct FAT32File* fileptr, uint8_t* location) {
    uint32_t* addr = fat32_linkedlist;
    uint32_t caddr;
    uint32_t nbytes = 0;
    uint8_t i=0;

    if(fileptr->filesize < 0x7500) { // 29 KiB
        if(fs_fat32_build_linked_list(fileptr->cluster) != 0x00) {
            return 0xFF;
        }
        if(fat32_chain_overflow) {
            return 0xFF;
        }

        while(*addr != 0xFFFFFFFF && nbytes < fileptr->filesize) {
            caddr = fs_fat32_calculate_sector_address(*(addr++), 0);

            for(i=0; i<fat32_partition.sectors_per_cluster; i++) {
                if(read_sector(caddr) != 0x00) {
                    return 0xFF;
                }
                caddr++;
                memcpy(location, (uint8_t*)SDBUF, 0x200);
                location += 0x200;

                nbytes += 0x200;
                if(nbytes >= fileptr->filesize) {
                    break;
                }
            }
        }
    }
    return 0;
}

/**
 * @brief Build a linked list of sector addresses starting from a root address
 * 
 * @param nextcluster first cluster in the linked list
 */
static uint8_t fs_fat32_build_linked_list(uint32_t nextcluster) {
    // counter over clusters
    uint8_t ctr = 0;
    uint8_t item = 0;

    // clear previous linked list
    memset(fat32_linkedlist, 0xFF, F32_LLSZ * sizeof(uint32_t));
    fat32_chain_overflow = 0;

    // try grabbing next cluster
    while(nextcluster < 0x0FFFFFF8 && nextcluster != 0 && ctr < F32_LLSZ) {
        fat32_linkedlist[ctr] = nextcluster;
        if(read_sector(fat32_partition.fat_begin_lba + (nextcluster >> 7)) != 0x00) {
            return 0xFF;
        }
        item = nextcluster & 0b01111111;
        nextcluster = *(uint32_t*)(SDBUF + item * 4);
        ctr++;
    }

    if(nextcluster < 0x0FFFFFF8 && nextcluster != 0 && ctr >= F32_LLSZ) {
        fat32_chain_overflow = 1;
    }

    return 0;
}

/**
 * @brief Mirror a written FAT sector to FAT#2.
 */
static uint8_t fs_fat32_mirror_fat_sector(uint32_t fat_sector_lba) {
    return write_sector(fat32_partition.fat2_begin_lba + (fat_sector_lba - fat32_partition.fat_begin_lba));
}

/**
 * @brief Set one FAT32 entry and mirror to FAT#2.
 */
static uint8_t fs_fat32_set_fat_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_sector_lba = fat32_partition.fat_begin_lba + (cluster >> 7);
    uint8_t item = cluster & 0x7F;

    if(read_sector(fat_sector_lba) != 0x00) {
        return 0xFF;
    }
    *(uint32_t*)(SDBUF + ((uint16_t)item << 2)) = (value & 0x0FFFFFFFUL);

    if(write_sector(fat_sector_lba) != 0x00) {
        return 0xFF;
    }
    if(fs_fat32_mirror_fat_sector(fat_sector_lba) != 0x00) {
        return 0xFF;
    }

    return 0;
}

/**
 * @brief Find first free FAT cluster.
 */
static uint8_t fs_fat32_find_free_cluster(uint32_t* cluster_out) {
    uint8_t i;
    uint32_t cluster;
    uint32_t entry;

    if(cluster_out == NULL) {
        return 0xFF;
    }

    if(read_sector(fat32_partition.fat_begin_lba) != 0x00) {
        return 0xFF;
    }

    for(i = 3; i < 128; i++) {
        cluster = i;
        if(cluster == fat32_partition.root_dir_first_cluster) {
            continue;
        }
        entry = *(uint32_t*)(SDBUF + ((uint16_t)i << 2)) & 0x0FFFFFFF;
        if(entry == 0) {
            *cluster_out = cluster;
            return 0;
        }
    }

    return 0xFF;
}

/**
 * @brief Construct sector address from file entry
 * 
 * @return uint32_t sector address
 */
static uint32_t fs_fat32_grab_cluster_address_from_fileblock(uint8_t *loc) {
    return ((uint32_t)*(uint16_t*)(loc + 0x14)) << 16 | 
                      *(uint16_t*)(loc + 0x1A);
}

/**
 * @brief Compare two files
 *
 * This routine is used for both searching a file/folder as well as for
 * sorting files.
 */
static int fs_fat32_file_compare(const void* item1, const void *item2) {
    const struct FAT32File *file1 = (const struct FAT32File*)item1;
    const struct FAT32File *file2 = (const struct FAT32File*)item2;

    // Extract directory bit (1 if directory, 0 otherwise)
    uint8_t is_dir1 = (file1->attrib & MASK_DIR) != 0;
    uint8_t is_dir2 = (file2->attrib & MASK_DIR) != 0;

    // Sort directories before files
    if (is_dir1 && !is_dir2) {
        return -1;
    } else if (!is_dir1 && is_dir2) {
        return 1;
    }

    // if same type, compare names
    return strcmp(file1->basename, file2->basename);
}

#pragma rodata-name(pop)
#pragma code-name(pop)
