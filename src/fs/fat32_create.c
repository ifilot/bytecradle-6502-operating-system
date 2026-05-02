#include "fat32_internal.h"
#include "constants.h"

#pragma code-name(push, "B3CODE")
#pragma rodata-name(push, "B3RODATA")

static uint32_t fs_create_cluster_lba(uint32_t cluster, uint8_t sector) {
    return fat32_partition.sector_begin_lba +
        (cluster - 2) * fat32_partition.sectors_per_cluster + sector;
}

/**
 * @brief Resident mkdir implementation; caller must already have BANK2 selected.
 */
uint8_t fs_mkdir_impl(const char* name) {
    char name83[12];
    uint32_t new_cluster;
    uint32_t dir_lba;
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

    if(fs_fat32_find_free_dir_slot(name83, &dir_lba, &dir_slot) != 0x00) {
        return 0xFF;
    }
    if(fs_fat32_find_free_cluster(&new_cluster) != 0x00) {
        return 0xFF;
    }
    if(fs_fat32_set_fat_entry(new_cluster, 0x0FFFFFFF) != 0x00) {
        return 0xFF;
    }

    if(fs_fat32_clear_cluster(new_cluster) != 0x00) {
        fs_fat32_set_fat_entry(new_cluster, 0x00000000);
        return 0xFF;
    }

    if(read_sector(fs_create_cluster_lba(new_cluster, 0)) != 0x00) {
        return 0xFF;
    }
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
    if(write_sector(fs_create_cluster_lba(new_cluster, 0)) != 0x00) {
        fs_fat32_set_fat_entry(new_cluster, 0x00000000);
        return 0xFF;
    }

    if(read_sector(dir_lba) != 0x00) {
        fs_fat32_set_fat_entry(new_cluster, 0x00000000);
        return 0xFF;
    }
    fs_fat32_write_dir_entry(dir_slot, name83, MASK_DIR, new_cluster);
    if(write_sector(dir_lba) != 0x00) {
        fs_fat32_set_fat_entry(new_cluster, 0x00000000);
        return 0xFF;
    }

    return 0;
}

/**
 * @brief Resident touch implementation; caller must already have BANK2 selected.
 */
uint8_t fs_touch_impl(const char* filename) {
    char name83[12];
    uint32_t dir_lba;
    uint8_t dir_slot = 0xFF;

    if(filename == NULL) {
        return 0xFF;
    }

    if(fs_make_83_filename(filename, name83) != 0x00) {
        return 0xFF;
    }
    name83[11] = 0x00;

    if(fs_fat32_find_free_dir_slot(name83, &dir_lba, &dir_slot) != 0x00) {
        return 0xFF;
    }
    if(read_sector(dir_lba) != 0x00) {
        return 0xFF;
    }
    fs_fat32_write_dir_entry(dir_slot, name83, 0x20, 0);
    if(write_sector(dir_lba) != 0x00) {
        return 0xFF;
    }

    fs_last_created_dir_lba = dir_lba;
    fs_last_created_dir_slot = dir_slot;
    return 0;
}

fs_fd_t fs_create_impl(const char* filename) {
    fs_fd_t fd;

    fs_last_created_dir_lba = 0;
    fs_last_created_dir_slot = 0xFF;
    if(fs_touch_impl(filename) != 0x00) {
        return -1;
    }

    fd = fs_open(filename);
    if(fd < 0) {
        return -1;
    }

    fs_open_files[fd].mode = FS_MODE_WRITE;
    fs_open_files[fd].dir_lba = fs_last_created_dir_lba;
    fs_open_files[fd].dir_slot = fs_last_created_dir_slot;
    return fd;
}

#pragma rodata-name(pop)
#pragma code-name(pop)
