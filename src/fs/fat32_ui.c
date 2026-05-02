#include "fat32_internal.h"

#pragma code-name(push, "CODE")
#pragma rodata-name(push, "RODATA")

static uint8_t fs_fat32_wait_for_listing_keypress(void);

void fs_fat32_print_partition_info(void) {
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

    sprintf(buf, "Volume name: %.11s\0", fat32_partition.volume_label);
    putstrnl(buf);
}

void fs_fat32_sort_files(void) {
    qsort(fat32_files, fat32_current_folder.nrfiles, sizeof(struct FAT32File), fs_fat32_file_compare);
    fs_dir_sorted = 1;
}

void fs_fat32_list_dir(void) {
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

static uint8_t fs_fat32_wait_for_listing_keypress(void) {
    uint8_t c;
    do {
        c = getch();
    } while(c == 0);

    return (uint8_t)(c == 'q' || c == 'Q');
}

int fs_fat32_file_compare(const void* item1, const void *item2) {
    const struct FAT32File *file1 = (const struct FAT32File*)item1;
    const struct FAT32File *file2 = (const struct FAT32File*)item2;
    uint8_t is_dir1 = (file1->attrib & MASK_DIR) != 0;
    uint8_t is_dir2 = (file2->attrib & MASK_DIR) != 0;

    if(is_dir1 && !is_dir2) {
        return -1;
    } else if(!is_dir1 && is_dir2) {
        return 1;
    }

    return strcmp(file1->basename, file2->basename);
}

#pragma rodata-name(pop)
#pragma code-name(pop)
