#include "fat32_internal.h"

#pragma code-name(push, "B2CODE")
#pragma rodata-name(push, "B2RODATA")

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

uint8_t fs_mount_impl(void) {
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

uint8_t fs_chdir_impl(const char* path) {
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

uint8_t fs_list_dir_impl(void) {
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

uint8_t fs_getcwd_impl(char* out, uint8_t out_sz) {
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

uint8_t fs_load_file_impl(const char* filename, uint8_t* location, uint32_t* filesize_out) {
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
    if(res == NULL || res->filesize >= 0x7500) {
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

fs_fd_t fs_open_impl(const char* filename) {
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
    if(res == NULL || (res->attrib & MASK_DIR)) {
        return -1;
    }

    for(fd=0; fd<FS_MAX_OPEN_FILES; fd++) {
        if(!fs_open_files[fd].in_use) {
            fs_open_files[fd].in_use = 1;
            fs_open_files[fd].mode = FS_MODE_READ;
            fs_open_files[fd].file = *res;
            fs_open_files[fd].offset = 0;
            fs_open_files[fd].dir_lba = 0;
            fs_open_files[fd].dir_slot = 0xFF;
            fs_open_files[fd].cluster_count = 0;
            fs_open_files[fd].cluster_overflow = 0;
            if(res->filesize != 0 && fs_build_cluster_cache(&fs_open_files[fd]) != 0x00) {
                fs_open_files[fd].in_use = 0;
                return -1;
            }
            return fd;
        }
    }

    return -1;
}

uint16_t fs_read_impl(fs_fd_t fd, uint8_t* dst, uint16_t len) {
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
    if(!handle->in_use || handle->cluster_overflow ||
       handle->offset >= handle->file.filesize ||
       fat32_partition.sectors_per_cluster == 0) {
        return 0;
    }

    remaining = handle->file.filesize - handle->offset;
    if(remaining > len) {
        remaining = len;
    }

    while(remaining > 0) {
        sector_in_file = handle->offset >> 9;
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

void fs_close_impl(fs_fd_t fd) {
    if(fd < 0 || fd >= FS_MAX_OPEN_FILES) {
        return;
    }
    fs_open_files[fd].in_use = 0;
    fs_open_files[fd].mode = 0;
    fs_open_files[fd].offset = 0;
    fs_open_files[fd].dir_lba = 0;
    fs_open_files[fd].dir_slot = 0xFF;
    fs_open_files[fd].cluster_count = 0;
    fs_open_files[fd].cluster_overflow = 0;
}

void fs_print_partition_info_impl(void) {
    fs_fat32_print_partition_info();
}

#pragma rodata-name(pop)
#pragma code-name(pop)

#pragma code-name(push, "B2CODE")
#pragma rodata-name(push, "B2RODATA")

uint8_t fs_stat_impl(struct BCOSFsStat* req) {
    struct FAT32File* res;
    char filename83[12];

    if(req == NULL || req->path == NULL) {
        return 0xFF;
    }
    if(fs_make_83_filename(req->path, filename83) != 0x00) {
        req->status = 0xFC;
        return req->status;
    }
    filename83[11] = 0x00;

    if(fs_fat32_read_dir() != 0x00) {
        req->status = 0xFF;
        return req->status;
    }

    res = fs_fat32_search_dir(filename83, FILE_ENTRY);
    if(res == NULL) {
        res = fs_fat32_search_dir(filename83, FOLDER_ENTRY);
    }
    if(res == NULL) {
        req->status = 0xFE;
        return req->status;
    }

    req->attrib = res->attrib;
    req->size = res->filesize;
    req->cluster = res->cluster;
    req->status = 0x00;
    return req->status;
}

uint8_t fs_readdir_impl(struct BCOSFsDirent* req) {
    struct FAT32File* file;
    uint8_t i;
    uint8_t j = 0;

    if(req == NULL) {
        return 0xFF;
    }
    if(fs_fat32_read_dir() != 0x00) {
        req->status = 0xFF;
        return req->status;
    }
    if(req->index >= fat32_current_folder.nrfiles) {
        req->status = 0xFB;
        return req->status;
    }

    file = &fat32_files[req->index];
    for(i = 0; i < 8 && file->basename[i] != ' '; i++) {
        req->name[j++] = file->basename[i];
    }
    if((file->attrib & MASK_DIR) == 0 && file->extension[0] != ' ') {
        req->name[j++] = '.';
        for(i = 0; i < 3 && file->extension[i] != ' '; i++) {
            req->name[j++] = file->extension[i];
        }
    }
    req->name[j] = 0x00;
    req->attrib = file->attrib;
    req->size = file->filesize;
    req->cluster = file->cluster;
    req->status = 0x00;
    return req->status;
}

#pragma rodata-name(pop)
#pragma code-name(pop)
