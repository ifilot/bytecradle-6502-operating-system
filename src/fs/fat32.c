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
};

static struct FSOpenFile fs_open_files[FS_MAX_OPEN_FILES];
static uint8_t fs_dir_sorted = 0;
static uint8_t fs_dir_overflow = 0;

static uint8_t fs_mount_impl(void);
static uint8_t fs_chdir_impl(const char* path);
static uint8_t fs_list_dir_impl(void);
static uint8_t fs_getcwd_impl(char* out, uint8_t out_sz);
static uint8_t fs_load_file_impl(const char* filename, uint8_t* location, uint32_t* filesize_out);
static fs_fd_t fs_open_impl(const char* filename);
static uint16_t fs_read_impl(fs_fd_t fd, uint8_t* dst, uint16_t len);
static void fs_close_impl(fs_fd_t fd);
static void fs_print_partition_info_impl(void);

static uint8_t fs_fat32_read_mbr(void);
static void fs_fat32_read_partition(void);
static void fs_fat32_print_partition_info(void);
static void fs_fat32_read_dir(void);
static void fs_fat32_sort_files(void);
static void fs_fat32_list_dir(void);
static struct FAT32File* fs_fat32_search_dir(const char* filename, uint8_t entry_type);
static void fs_fat32_load_file(const struct FAT32File* fileptr, uint8_t* location);
static void fs_fat32_build_linked_list(uint32_t nextcluster);
static uint32_t fs_fat32_grab_cluster_address_from_fileblock(uint8_t *loc);
static uint32_t fs_fat32_calculate_sector_address(uint32_t cluster, uint8_t sector);
static int fs_fat32_file_compare(const void* item1, const void *item2);

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
    uint8_t i = 0;
    uint8_t j = 0;

    if(filename == NULL || out == NULL || *filename == '\0') {
        return 0xFF;
    }

    memset(out, ' ', 11);
    dot = strchr(filename, '.');

    while(filename[i] != '\0' && filename[i] != '.' && i < 8) {
        char c = filename[i];
        if(c >= 'a' && c <= 'z') {
            c -= 32;
        }
        out[i] = c;
        i++;
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

    fs_fat32_build_linked_list(handle->file.cluster);
    handle->cluster_count = 0;
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

    fs_fat32_read_partition();
    memset(fs_open_files, 0x00, sizeof(fs_open_files));
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

    fs_fat32_read_dir();
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
    fs_fat32_read_dir();
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

    fs_fat32_read_dir();
    res = fs_fat32_search_dir(filename83, FILE_ENTRY);
    if(res == NULL) {
        return 0xFF;
    }

    if(res->filesize >= 0x7500) {
        return 0xFF;
    }

    fs_fat32_load_file(res, location);
    if(filesize_out != NULL) {
        *filesize_out = res->filesize;
    }
    return 0;
}

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

    fs_fat32_read_dir();
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
    uint16_t checksum_sd = 0x0000;
    uint16_t checksum = 0x0000;
    uint8_t* sdbuf = (uint8_t*)(SDBUF);

    read_sector(0x00000000);

    // check partition type
    if(sdbuf[0x1C2] != 0x0C) {
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
static void fs_fat32_read_partition(void) {
    // extract logical block address (LBA) from partition table
    uint32_t lba = *(uint32_t*)(SDBUF + 0x1C6);

    // retrieve the partition table
    read_sector(lba);

    // store partition information
    fat32_partition.bytes_per_sector = *(uint16_t*)(SDBUF + 0x0B);
    fat32_partition.sectors_per_cluster = *(uint8_t*)(SDBUF + 0x0D);
    fat32_partition.reserved_sectors = *(uint16_t*)(SDBUF + 0x0E);
    fat32_partition.number_of_fats = *(uint8_t*)(SDBUF + 0x10);
    fat32_partition.sectors_per_fat = *(uint32_t*)(SDBUF + 0x24);
    fat32_partition.root_dir_first_cluster = *(uint32_t*)(SDBUF + 0x2C);
    fat32_partition.fat_begin_lba = lba + fat32_partition.reserved_sectors;
    fat32_partition.sector_begin_lba = fat32_partition.fat_begin_lba + 
        (fat32_partition.number_of_fats * fat32_partition.sectors_per_fat);
    fat32_partition.lba_addr_root_dir = fs_fat32_calculate_sector_address(fat32_partition.root_dir_first_cluster, 0);

    // set the cluster of the current folder
    fat32_current_folder.cluster = fat32_partition.root_dir_first_cluster;
    fat32_current_folder.nrfiles = 0;
    memset(fat32_current_folder.name, 0x00, 11);
    fat32_root_folder = fat32_current_folder;
    fat32_fullpath[0] = fat32_root_folder;
    fat32_pathdepth = 1;

    // read first sector of first partition to establish volume name
    read_sector(fat32_partition.lba_addr_root_dir);

    // copy volume name
    memcpy(fat32_partition.volume_label, (uint8_t*)(SDBUF), 11);
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
static void fs_fat32_read_dir() {
    uint8_t ctr = 0;                // counter over clusters
    uint16_t fctr = 0;              // counter over directory entries (files and folders)
    uint8_t i = 0, j = 0;
    uint8_t* locptr = NULL;
    struct FAT32File *file = NULL;

    fs_dir_sorted = 0;
    fs_dir_overflow = 0;

    // build linked list
    fs_fat32_build_linked_list(fat32_current_folder.cluster);

    while(fat32_linkedlist[ctr] != 0xFFFFFFFF && ctr < F32_LLSZ) {
        // print cluster number and address
        uint32_t caddr = fs_fat32_calculate_sector_address(fat32_linkedlist[ctr], 0);

        // loop over all sectors per cluster
        for(i=0; i<fat32_partition.sectors_per_cluster; i++) {
            read_sector(caddr);            // read sector data
            locptr = (uint8_t*)(SDBUF);    // set pointer to sector data
            for(j=0; j<16; j++) { // 16 file tables per sector
                // continue if an unused entry is encountered 0xE5
                if(*locptr == 0xE5) {
                    locptr += 32;  // next file entry location
                    continue;
                }

                // early exit if a zero is read
                if(*locptr == 0x00) {
                    file = &fat32_files[fctr];
                    fat32_current_folder.nrfiles = fctr;
                    return;
                }

                // check if we are reading a file or a folder
                if((*(locptr + 0x0B) & 0x0F) == 0x00) {

                    file = &fat32_files[fctr];
                    memcpy(file->basename, locptr, 11);
                    file->termbyte = 0x00;  // by definition
                    file->attrib = *(locptr + 0x0B);
                    file->cluster = fs_fat32_grab_cluster_address_from_fileblock(locptr);
                    file->filesize = *(uint32_t*)(locptr + 0x1C);
                    fctr++;

                    if(fctr >= MAXFILES) {
                        fs_dir_overflow = 1;
                        putstrnl("Error: too many entries in folder; cannot parse...");
                        fat32_current_folder.nrfiles = fctr;
                        return;
                    }
                }
                locptr += 32;  // next file entry location
            }
            caddr++;    // next sector
        }
        ctr++;  // next cluster
    }
    fat32_current_folder.nrfiles = fctr;
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
    for(i = 0; i<fat32_current_folder.nrfiles; i++) {
        if(file->attrib & MASK_DIR) {
            sprintf(buf, "%.8s %.3s %08lX DIR", file->basename, file->extension, file->cluster);
        } else {
            sprintf(buf, "%.8s.%.3s %08lX %lu", file->basename, file->extension, file->cluster, file->filesize);
        }
        putstrnl(buf);
        file++;
    }
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
static void fs_fat32_load_file(const struct FAT32File* fileptr, uint8_t* location) {
    uint32_t* addr = fat32_linkedlist;
    uint32_t caddr;
    uint32_t nbytes = 0;
    uint8_t i=0;

    if(fileptr->filesize < 0x7500) { // 29 KiB
        fs_fat32_build_linked_list(fileptr->cluster);

        while(*addr != 0x0FFFFFFF && nbytes < fileptr->filesize) {
            caddr = fs_fat32_calculate_sector_address(*(addr++), 0);

            for(i=0; i<fat32_partition.sectors_per_cluster; i++) {
                read_sector(caddr);
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
}

/**
 * @brief Build a linked list of sector addresses starting from a root address
 * 
 * @param nextcluster first cluster in the linked list
 */
static void fs_fat32_build_linked_list(uint32_t nextcluster) {
    // counter over clusters
    uint8_t ctr = 0;
    uint8_t item = 0;

    // clear previous linked list
    memset(fat32_linkedlist, 0xFF, F32_LLSZ * sizeof(uint32_t));

    // try grabbing next cluster
    while(nextcluster < 0x0FFFFFF8 && nextcluster != 0 && ctr < F32_LLSZ) {
        fat32_linkedlist[ctr] = nextcluster;
        read_sector(fat32_partition.fat_begin_lba + (nextcluster >> 7));
        item = nextcluster & 0b01111111;
        nextcluster = *(uint32_t*)(SDBUF + item * 4);
        ctr++;
    }
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
