#include "fat32_internal.h"
#include "constants.h"

#pragma code-name(push, "B3CODE")
#pragma rodata-name(push, "B3RODATA")

static uint32_t fs_b3_cluster_lba(uint32_t cluster, uint8_t sector) {
    return fat32_partition.sector_begin_lba +
        (cluster - 2) * fat32_partition.sectors_per_cluster + sector;
}

static uint8_t fs_b3_read_fat(uint32_t cluster, uint32_t* value_out) {
    uint32_t fat_sector_lba = fat32_partition.fat_begin_lba + (cluster >> 7);
    uint8_t item = cluster & 0x7F;

    if(value_out == NULL || read_sector(fat_sector_lba) != 0x00) {
        return 0xFF;
    }
    *value_out = *(uint32_t*)(SDBUF + ((uint16_t)item << 2)) & 0x0FFFFFFF;
    return 0;
}

static uint8_t fs_b3_mirror_fat_sector(uint32_t fat_sector_lba) {
    if(fat32_partition.number_of_fats < 2) {
        return 0;
    }
    return write_sector(fat32_partition.fat2_begin_lba + (fat_sector_lba - fat32_partition.fat_begin_lba));
}

static uint8_t fs_b3_set_fat(uint32_t cluster, uint32_t value) {
    uint32_t fat_sector_lba = fat32_partition.fat_begin_lba + (cluster >> 7);
    uint8_t item = cluster & 0x7F;
    uint8_t* entry;

    if(read_sector(fat_sector_lba) != 0x00) {
        return 0xFF;
    }
    entry = (uint8_t*)SDBUF + ((uint16_t)item << 2);
    *(uint32_t*)entry = (*(uint32_t*)entry & 0xF0000000UL) | (value & 0x0FFFFFFFUL);
    if(write_sector(fat_sector_lba) != 0x00) {
        return 0xFF;
    }
    return fs_b3_mirror_fat_sector(fat_sector_lba);
}

static uint8_t fs_b3_find_free_cluster(uint32_t* cluster_out) {
    uint32_t fat_sector;
    uint8_t i;
    uint32_t cluster;
    uint32_t entry;

    if(cluster_out == NULL) {
        return 0xFF;
    }

    for(fat_sector = 0; fat_sector < fat32_partition.sectors_per_fat; fat_sector++) {
        if(read_sector(fat32_partition.fat_begin_lba + fat_sector) != 0x00) {
            return 0xFF;
        }
        for(i = 0; i < 128; i++) {
            cluster = (fat_sector << 7) + i;
            if(cluster < 2 || cluster == fat32_partition.root_dir_first_cluster) {
                continue;
            }
            entry = *(uint32_t*)(SDBUF + ((uint16_t)i << 2)) & 0x0FFFFFFF;
            if(entry == 0) {
                *cluster_out = cluster;
                return 0;
            }
        }
    }
    return 0xFF;
}

static uint8_t fs_b3_clear_cluster(uint32_t cluster) {
    uint8_t sector;

    for(sector = 0; sector < fat32_partition.sectors_per_cluster; sector++) {
        memset((uint8_t*)SDBUF, 0x00, 512);
        if(write_sector(fs_b3_cluster_lba(cluster, sector)) != 0x00) {
            return 0xFF;
        }
    }
    return 0;
}

static uint8_t fs_b3_alloc_cluster(uint32_t prev_cluster, uint32_t* new_cluster_out) {
    uint32_t new_cluster;

    if(new_cluster_out == NULL || fs_b3_find_free_cluster(&new_cluster) != 0x00) {
        return 0xFF;
    }
    if(fs_b3_set_fat(new_cluster, 0x0FFFFFFF) != 0x00) {
        return 0xFF;
    }
    if(fs_b3_clear_cluster(new_cluster) != 0x00) {
        fs_b3_set_fat(new_cluster, 0);
        return 0xFF;
    }
    if(prev_cluster != 0 && fs_b3_set_fat(prev_cluster, new_cluster) != 0x00) {
        fs_b3_set_fat(new_cluster, 0);
        return 0xFF;
    }
    *new_cluster_out = new_cluster;
    return 0;
}

static void fs_b3_write_dir_entry(uint8_t slot, const char* name83, uint8_t attrib,
                                  uint32_t cluster, uint32_t size) {
    uint8_t* entry = (uint8_t*)SDBUF + ((uint16_t)slot << 5);

    memset(entry, 0x00, 32);
    memcpy(entry, name83, 11);
    entry[0x0B] = attrib;
    *(uint16_t*)(entry + 0x14) = (uint16_t)(cluster >> 16);
    *(uint16_t*)(entry + 0x1A) = (uint16_t)(cluster & 0xFFFF);
    *(uint32_t*)(entry + 0x1C) = size;
}

static uint8_t fs_b3_cluster_for_offset(struct FSOpenFile* handle, uint32_t offset,
                                        uint32_t* cluster_out) {
    uint32_t cluster_index;
    uint32_t cluster;
    uint32_t next;
    uint32_t i;

    if(handle == NULL || cluster_out == NULL || fat32_partition.sectors_per_cluster == 0) {
        return 0xFF;
    }

    cluster_index = (offset >> 9) / fat32_partition.sectors_per_cluster;
    if(handle->file.cluster == 0) {
        if(fs_b3_alloc_cluster(0, &cluster) != 0x00) {
            return 0xFF;
        }
        handle->file.cluster = cluster;
    }

    cluster = handle->file.cluster;
    for(i = 0; i < cluster_index; i++) {
        if(fs_b3_read_fat(cluster, &next) != 0x00) {
            return 0xFF;
        }
        if(next >= 0x0FFFFFF8 || next == 0) {
            if(fs_b3_alloc_cluster(cluster, &next) != 0x00) {
                return 0xFF;
            }
        }
        cluster = next;
    }
    *cluster_out = cluster;
    return 0;
}

uint16_t fs_write_impl(fs_fd_t fd, const uint8_t* src, uint16_t len) {
    struct FSOpenFile* handle;
    uint16_t nwritten = 0;
    uint16_t copy_n;
    uint16_t sector_offset;
    uint8_t sector_in_cluster;
    uint32_t cluster;
    uint32_t lba;

    if(fd < 0 || fd >= FS_MAX_OPEN_FILES || src == NULL || len == 0) {
        return 0;
    }
    handle = &fs_open_files[fd];
    if(!handle->in_use || handle->mode != FS_MODE_WRITE ||
       fat32_partition.sectors_per_cluster == 0) {
        return 0;
    }

    while(nwritten < len) {
        if(fs_b3_cluster_for_offset(handle, handle->offset, &cluster) != 0x00) {
            break;
        }
        sector_in_cluster = (uint8_t)((handle->offset >> 9) % fat32_partition.sectors_per_cluster);
        sector_offset = (uint16_t)(handle->offset & 0x1FF);
        lba = fs_b3_cluster_lba(cluster, sector_in_cluster);

        copy_n = 0x200 - sector_offset;
        if(copy_n > (len - nwritten)) {
            copy_n = len - nwritten;
        }

        if(copy_n != 0x200 || sector_offset != 0) {
            if(read_sector(lba) != 0x00) {
                break;
            }
        } else {
            memset((uint8_t*)SDBUF, 0x00, 512);
        }
        memcpy((uint8_t*)SDBUF + sector_offset, src + nwritten, copy_n);
        if(write_sector(lba) != 0x00) {
            break;
        }

        nwritten += copy_n;
        handle->offset += copy_n;
        if(handle->offset > handle->file.filesize) {
            handle->file.filesize = handle->offset;
        }
    }

    if(nwritten != 0) {
        fs_flush_impl(fd);
    }
    return nwritten;
}

uint8_t fs_flush_impl(fs_fd_t fd) {
    struct FSOpenFile* handle;
    char name83[12];

    if(fd < 0 || fd >= FS_MAX_OPEN_FILES) {
        return 0xFA;
    }
    handle = &fs_open_files[fd];
    if(!handle->in_use || handle->mode != FS_MODE_WRITE || handle->dir_slot >= 16) {
        return 0xFA;
    }
    if(read_sector(handle->dir_lba) != 0x00) {
        return 0xFF;
    }
    memcpy(name83, handle->file.basename, 8);
    memcpy(name83 + 8, handle->file.extension, 3);
    fs_b3_write_dir_entry(handle->dir_slot, name83, handle->file.attrib,
                          handle->file.cluster, handle->file.filesize);
    if(write_sector(handle->dir_lba) != 0x00) {
        return 0xFF;
    }
    return 0;
}

#pragma rodata-name(pop)
#pragma code-name(pop)
