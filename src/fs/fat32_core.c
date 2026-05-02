#include "fat32_internal.h"
#include "constants.h"

static uint32_t fs_fat32_grab_cluster_address_from_fileblock(uint8_t *loc);

uint8_t fs_make_83_filename(const char* filename, char* out) {
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

#pragma code-name(push, "B2CODE")
#pragma rodata-name(push, "B2RODATA")

uint8_t fs_fat32_read_mbr(void) {
    uint8_t* sdbuf = (uint8_t*)(SDBUF);

    if(read_sector(0x00000000) != 0x00) {
        return 0xFF;
    }
    if(sdbuf[0x1C2] != 0x0C && sdbuf[0x1C2] != 0x0B) {
        return 0xFF;
    }
    if(sdbuf[0x1FE] != 0x55 || sdbuf[0x1FF] != 0xAA) {
        return 0xFF;
    }

    return 0;
}

uint8_t fs_fat32_read_partition(void) {
    uint32_t lba = *(uint32_t*)(SDBUF + 0x1C6);

    if(read_sector(lba) != 0x00) {
        return 0xFF;
    }

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

    if(fat32_partition.bytes_per_sector != 512 ||
       fat32_partition.sectors_per_cluster == 0 ||
       (fat32_partition.sectors_per_cluster & (fat32_partition.sectors_per_cluster - 1)) != 0 ||
       fat32_partition.number_of_fats == 0 ||
       fat32_partition.sectors_per_fat == 0 ||
       fat32_partition.root_dir_first_cluster < 2) {
        return 0xFF;
    }

    fat32_current_folder.cluster = fat32_partition.root_dir_first_cluster;
    fat32_current_folder.nrfiles = 0;
    memset(fat32_current_folder.name, 0x00, 11);
    fat32_root_folder = fat32_current_folder;
    fat32_fullpath[0] = fat32_root_folder;
    fat32_pathdepth = 1;

    if(read_sector(fat32_partition.lba_addr_root_dir) != 0x00) {
        return 0xFF;
    }

    memcpy(fat32_partition.volume_label, (uint8_t*)(SDBUF), 11);
    return 0;
}

uint8_t fs_fat32_read_dir(void) {
    uint8_t ctr = 0;
    uint16_t fctr = 0;
    uint8_t i = 0, j = 0;
    uint8_t* locptr = NULL;
    struct FAT32File *file = NULL;

    fs_dir_sorted = 0;
    fs_dir_overflow = 0;

    if(fs_fat32_build_linked_list(fat32_current_folder.cluster) != 0x00) {
        return 0xFF;
    }

    while(ctr < F32_LLSZ && fat32_linkedlist[ctr] != 0xFFFFFFFF) {
        uint32_t caddr = fs_fat32_calculate_sector_address(fat32_linkedlist[ctr], 0);
        for(i=0; i<fat32_partition.sectors_per_cluster; i++) {
            if(read_sector(caddr) != 0x00) {
                return 0xFF;
            }
            locptr = (uint8_t*)(SDBUF);
            for(j=0; j<16; j++) {
                if(*locptr == 0xE5) {
                    locptr += 32;
                    continue;
                }
                if(*locptr == 0x00) {
                    fat32_current_folder.nrfiles = fctr;
                    return 0;
                }
                if((*(locptr + 0x0B) & 0x0F) == 0x0F) {
                    locptr += 32;
                    continue;
                }
                if((*(locptr + 0x0B) & 0x08) == 0x00) {
                    if(fctr >= MAXFILES) {
                        fs_dir_overflow = 1;
                        putstrnl("Error: too many entries in folder; cannot parse...");
                        fat32_current_folder.nrfiles = fctr;
                        return 0xFF;
                    }
                    file = &fat32_files[fctr];
                    memcpy(file->basename, locptr, 11);
                    file->termbyte = 0x00;
                    file->attrib = *(locptr + 0x0B);
                    file->cluster = fs_fat32_grab_cluster_address_from_fileblock(locptr);
                    file->filesize = *(uint32_t*)(locptr + 0x1C);
                    fctr++;
                }
                locptr += 32;
            }
            caddr++;
        }
        ctr++;
    }
    fat32_current_folder.nrfiles = fctr;
    return fat32_chain_overflow ? 0xFF : 0;
}

struct FAT32File* fs_fat32_search_dir(const char* filename, uint8_t entry_type) {
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

uint32_t fs_fat32_calculate_sector_address(uint32_t cluster, uint8_t sector) {
    return fat32_partition.sector_begin_lba + (cluster - 2) * fat32_partition.sectors_per_cluster + sector;
}

uint8_t fs_fat32_load_file(const struct FAT32File* fileptr, uint8_t* location) {
    uint32_t* addr = fat32_linkedlist;
    uint32_t caddr;
    uint32_t nbytes = 0;
    uint8_t i=0;

    if(fileptr->filesize < 0x7500) {
        if(fs_fat32_build_linked_list(fileptr->cluster) != 0x00 || fat32_chain_overflow) {
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

uint8_t fs_fat32_build_linked_list(uint32_t nextcluster) {
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

static uint32_t fs_fat32_grab_cluster_address_from_fileblock(uint8_t *loc) {
    return ((uint32_t)*(uint16_t*)(loc + 0x14)) << 16 |
                      *(uint16_t*)(loc + 0x1A);
}

#pragma rodata-name(pop)
#pragma code-name(pop)
