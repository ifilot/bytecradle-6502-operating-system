#ifndef _FAT32_INTERNAL_H
#define _FAT32_INTERNAL_H

#include "fat32.h"

#define SD_ROM_BANK 2
#define FS_WRITE_ROM_BANK 3
#define COMMAND_ROM_BANK 4
#define FS_MAX_OPEN_FILES 4
#define FS_SD_BOOT_RETRIES 10
#define FS_MAX_PATH_DEPTH 16

struct FSOpenFile {
    uint8_t in_use;
    uint8_t mode;
    struct FAT32File file;
    uint32_t offset;
    uint32_t dir_lba;
    uint8_t dir_slot;
    uint32_t clusters[F32_LLSZ];
    uint8_t cluster_count;
    uint8_t cluster_overflow;
};

extern struct FAT32Partition fat32_partition;
extern uint32_t fat32_linkedlist[F32_LLSZ];
extern struct FAT32Folder fat32_current_folder;
extern struct FAT32Folder fat32_root_folder;
extern struct FAT32File *fat32_files;
extern struct FAT32Folder *fat32_fullpath;
extern uint8_t fat32_pathdepth;
extern struct FSOpenFile fs_open_files[FS_MAX_OPEN_FILES];
extern uint8_t fs_dir_sorted;
extern uint8_t fs_dir_overflow;
extern uint8_t fat32_chain_overflow;
extern uint32_t fs_last_created_dir_lba;
extern uint8_t fs_last_created_dir_slot;

uint8_t fs_mkdir_impl(const char* name);
uint8_t fs_touch_impl(const char* filename);
uint8_t fs_mount_impl(void);
uint8_t fs_chdir_impl(const char* path);
uint8_t fs_list_dir_impl(void);
uint8_t fs_getcwd_impl(char* out, uint8_t out_sz);
uint8_t fs_load_file_impl(const char* filename, uint8_t* location, uint32_t* filesize_out);
fs_fd_t fs_open_impl(const char* filename);
uint16_t fs_read_impl(fs_fd_t fd, uint8_t* dst, uint16_t len);
fs_fd_t fs_create_impl(const char* filename);
uint16_t fs_write_impl(fs_fd_t fd, const uint8_t* src, uint16_t len);
uint8_t fs_flush_impl(fs_fd_t fd);
void fs_close_impl(fs_fd_t fd);
void fs_print_partition_info_impl(void);
uint8_t fs_stat_impl(struct BCOSFsStat* req);
uint8_t fs_readdir_impl(struct BCOSFsDirent* req);
void fs_syscall_stack_reset(void);

void fs_fat32_print_partition_info(void);
void fs_fat32_sort_files(void);
void fs_fat32_list_dir(void);
int fs_fat32_file_compare(const void* item1, const void *item2);

uint8_t fs_make_83_filename(const char* filename, char* out);
uint8_t fs_fat32_read_mbr(void);
uint8_t fs_fat32_read_partition(void);
uint8_t fs_fat32_read_dir(void);
struct FAT32File* fs_fat32_search_dir(const char* filename, uint8_t entry_type);
uint32_t fs_fat32_calculate_sector_address(uint32_t cluster, uint8_t sector);
uint8_t fs_fat32_load_file(const struct FAT32File* fileptr, uint8_t* location);
uint8_t fs_fat32_build_linked_list(uint32_t nextcluster);
uint8_t fs_fat32_find_free_cluster(uint32_t* cluster_out);
uint8_t fs_fat32_set_fat_entry(uint32_t cluster, uint32_t value);
uint8_t fs_fat32_clear_cluster(uint32_t cluster);
uint8_t fs_fat32_find_free_dir_slot(const char* name83, uint32_t* dir_lba_out, uint8_t* dir_slot_out);
void fs_fat32_write_dir_entry(uint8_t slot, const char* name83, uint8_t attrib, uint32_t cluster);

#endif
