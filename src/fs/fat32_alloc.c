#include "fat32_internal.h"
#include "constants.h"

#pragma code-name(push, "B3CODE")
#pragma rodata-name(push, "B3RODATA")

static uint8_t fs_fat32_mirror_fat_sector(uint32_t fat_sector_lba);
static uint8_t fs_fat32_extend_current_dir(uint32_t* dir_lba_out, uint8_t* dir_slot_out);

static uint32_t fs_fat32_alloc_cluster_lba(uint32_t cluster, uint8_t sector) {
    return fat32_partition.sector_begin_lba +
        (cluster - 2) * fat32_partition.sectors_per_cluster + sector;
}

static uint8_t fs_fat32_alloc_build_linked_list(uint32_t nextcluster) {
    uint8_t ctr = 0;
    uint8_t item = 0;

    memset(fat32_linkedlist, 0xFF, F32_LLSZ * sizeof(uint32_t));
    fat32_chain_overflow = 0;

    while(nextcluster < 0x0FFFFFF8 && nextcluster != 0 && ctr < F32_LLSZ) {
        fat32_linkedlist[ctr] = nextcluster;
        if(read_sector(fat32_partition.fat_begin_lba + (nextcluster >> 7)) != 0x00) {
            return 0xFF;
        }
        item = nextcluster & 0x7F;
        nextcluster = *(uint32_t*)(SDBUF + ((uint16_t)item << 2)) & 0x0FFFFFFF;
        ctr++;
    }

    if(nextcluster < 0x0FFFFFF8 && nextcluster != 0 && ctr >= F32_LLSZ) {
        fat32_chain_overflow = 1;
    }

    return 0;
}

static uint8_t fs_fat32_mirror_fat_sector(uint32_t fat_sector_lba) {
    if(fat32_partition.number_of_fats < 2) {
        return 0;
    }
    return write_sector(fat32_partition.fat2_begin_lba + (fat_sector_lba - fat32_partition.fat_begin_lba));
}

uint8_t fs_fat32_set_fat_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_sector_lba = fat32_partition.fat_begin_lba + (cluster >> 7);
    uint8_t item = cluster & 0x7F;

    if(read_sector(fat_sector_lba) != 0x00) {
        return 0xFF;
    }
    *(uint32_t*)(SDBUF + ((uint16_t)item << 2)) =
        (*(uint32_t*)(SDBUF + ((uint16_t)item << 2)) & 0xF0000000UL) | (value & 0x0FFFFFFFUL);

    if(write_sector(fat_sector_lba) != 0x00) {
        return 0xFF;
    }
    if(fs_fat32_mirror_fat_sector(fat_sector_lba) != 0x00) {
        return 0xFF;
    }

    return 0;
}

uint8_t fs_fat32_find_free_cluster(uint32_t* cluster_out) {
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

uint8_t fs_fat32_clear_cluster(uint32_t cluster) {
    uint8_t i;

    for(i = 0; i < fat32_partition.sectors_per_cluster; i++) {
        memset((uint8_t*)SDBUF, 0x00, 512);
        if(write_sector(fs_fat32_alloc_cluster_lba(cluster, i)) != 0x00) {
            return 0xFF;
        }
    }

    return 0;
}

uint8_t fs_fat32_find_free_dir_slot(const char* name83, uint32_t* dir_lba_out, uint8_t* dir_slot_out) {
    uint8_t ctr = 0;
    uint8_t sector;
    uint8_t slot;
    uint32_t dir_lba;
    uint8_t found_free = 0;

    if(name83 == NULL || dir_lba_out == NULL || dir_slot_out == NULL) {
        return 0xFF;
    }
    if(fs_fat32_alloc_build_linked_list(fat32_current_folder.cluster) != 0x00) {
        return 0xFF;
    }

    while(ctr < F32_LLSZ && fat32_linkedlist[ctr] != 0xFFFFFFFF) {
        for(sector = 0; sector < fat32_partition.sectors_per_cluster; sector++) {
            dir_lba = fs_fat32_alloc_cluster_lba(fat32_linkedlist[ctr], sector);
            if(read_sector(dir_lba) != 0x00) {
                return 0xFF;
            }
            for(slot = 0; slot < 16; slot++) {
                uint8_t* entry = (uint8_t*)SDBUF + ((uint16_t)slot << 5);
                uint8_t first = *entry;
                if(first != 0x00 && first != 0xE5 &&
                   (entry[0x0B] & 0x0F) != 0x0F &&
                   (entry[0x0B] & 0x08) == 0x00 &&
                   memcmp(entry, name83, 11) == 0) {
                    return 0xFF;
                }
                if(first == 0x00 || first == 0xE5) {
                    if(!found_free) {
                        *dir_lba_out = dir_lba;
                        *dir_slot_out = slot;
                        found_free = 1;
                    }
                    if(first == 0x00) {
                        return 0;
                    }
                }
            }
        }
        ctr++;
    }

    if(found_free) {
        return 0;
    }

    return fs_fat32_extend_current_dir(dir_lba_out, dir_slot_out);
}

void fs_fat32_write_dir_entry(uint8_t slot, const char* name83, uint8_t attrib, uint32_t cluster) {
    uint8_t* entry = (uint8_t*)SDBUF + ((uint16_t)slot << 5);

    memset(entry, 0x00, 32);
    memcpy(entry, name83, 11);
    entry[0x0B] = attrib;
    *(uint16_t*)(entry + 0x14) = (uint16_t)(cluster >> 16);
    *(uint16_t*)(entry + 0x1A) = (uint16_t)(cluster & 0xFFFF);
}

static uint8_t fs_fat32_extend_current_dir(uint32_t* dir_lba_out, uint8_t* dir_slot_out) {
    uint8_t ctr = 0;
    uint32_t last_cluster;
    uint32_t new_cluster;

    if(fat32_chain_overflow) {
        return 0xFF;
    }

    while(ctr < F32_LLSZ && fat32_linkedlist[ctr] != 0xFFFFFFFF) {
        ctr++;
    }
    if(ctr == 0 || ctr >= F32_LLSZ) {
        return 0xFF;
    }

    last_cluster = fat32_linkedlist[ctr - 1];
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
    if(fs_fat32_set_fat_entry(last_cluster, new_cluster) != 0x00) {
        fs_fat32_set_fat_entry(new_cluster, 0x00000000);
        return 0xFF;
    }

    *dir_lba_out = fs_fat32_alloc_cluster_lba(new_cluster, 0);
    *dir_slot_out = 0;
    return 0;
}

#pragma rodata-name(pop)
#pragma code-name(pop)
