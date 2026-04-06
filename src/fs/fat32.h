/**************************************************************************
 *                                                                        *
 *   Author: Ivo Filot <ivo@ivofilot.nl>                                  *
 *                                                                        *
 *   ByteCradle 6502 OS is free software:                                 *
 *   you can redistribute it and/or modify it under the terms of the      *
 *   GNU General Public License as published by the Free Software         *
 *   Foundation, either version 3 of the License, or (at your option)     *
 *   any later version.                                                   *
 *                                                                        *
 *   ByteCradle 6502 OS is distributed in the hope that it will be useful,*
 *   but WITHOUT ANY WARRANTY; without even the implied warranty          *
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.              *
 *   See the GNU General Public License for more details.                 *
 *                                                                        *
 *   You should have received a copy of the GNU General Public License    *
 *   along with this program.  If not, see http://www.gnu.org/licenses/.  *
 *                                                                        *
 **************************************************************************/

#ifndef _FAT32_H
#define _FAT32_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "sdcard.h"
#include "crc16.h"
#include "io.h"

#define F32_LLSZ 16

struct FAT32Partition {
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t number_of_fats;
    uint32_t sectors_per_fat;
    uint32_t root_dir_first_cluster;
    uint32_t fat_begin_lba;
    uint32_t fat2_begin_lba;
    uint32_t lba_addr_root_dir;
    uint32_t sector_begin_lba;
    char volume_label[11];
};

struct FAT32Folder {
    char name[11];
    uint32_t cluster;
    uint16_t nrfiles;
};

struct FAT32File {
    char basename[8];
    char extension[3];
    uint8_t termbyte;   // always zero
    uint8_t attrib;
    uint32_t cluster;
    uint32_t filesize;
};

typedef int8_t fs_fd_t;

#define MASK_DIR        (1 << 4)
#define FILE_ENTRY      0
#define FOLDER_ENTRY    1

#define MAXFILES        255
#define FAT32FILESLOC   (SDBUF + 0x300)
#define FAT32FOLDERLOC  (FAT32FILESLOC + sizeof(struct FAT32File) * MAXFILES)

/*
 * Note: public fs APIs are resident wrappers that switch ROM to BANK 2
 * for execution of SD/FAT logic, then restore the previous ROM bank.
 */

/**
 * @brief Mount filesystem (boot SD, read MBR and partition metadata)
 *
 * @return 0 on success, 0xFF on error
 */
uint8_t fs_mount(void);

/**
 * @brief Print FAT32 partition metadata to the console.
 */
void fs_print_partition_info(void);

/**
 * @brief Create a new child directory in the current folder.
 *
 * @param name folder name (8.3-compatible, no dot)
 * @return 0 on success, 0xFF on error
 */
uint8_t fs_mkdir(const char* name);

/**
 * @brief Create an empty file entry in the current folder.
 *
 * @param filename file name (8.3-compatible)
 * @return 0 on success, 0xFF on error
 */
uint8_t fs_touch(const char* filename);

/**
 * @brief Change current working directory.
 *
 * Supports "/", "..", and direct child folder names.
 *
 * @param path path argument
 * @return 0 on success, 0xFF on error
 */
uint8_t fs_chdir(const char* path);

/**
 * @brief List current working directory.
 *
 * @return 0 on success, 0xFF on error
 */
uint8_t fs_list_dir(void);

/**
 * @brief Retrieve current working directory into user buffer.
 *
 * @param out destination buffer
 * @param out_sz destination buffer size
 * @return 0 on success, 0xFF on overflow/error
 */
uint8_t fs_getcwd(char* out, uint8_t out_sz);

/**
 * @brief Load a file from current directory into memory.
 *
 * @param filename 8.3 filename (with or without dot)
 * @param location target memory
 * @param filesize_out optional file size output (can be NULL)
 * @return 0 on success, 0xFF on error
 */
uint8_t fs_load_file(const char* filename, uint8_t* location, uint32_t* filesize_out);

/**
 * @brief Open file in current directory.
 *
 * @param filename 8.3 filename
 * @return file descriptor [0..3] or -1 on failure
 */
fs_fd_t fs_open(const char* filename);

/**
 * @brief Read bytes from open file descriptor.
 *
 * @param fd file descriptor from fs_open
 * @param dst destination buffer
 * @param len number of bytes requested
 * @return number of bytes read
 */
uint16_t fs_read(fs_fd_t fd, uint8_t* dst, uint16_t len);

/**
 * @brief Close open file descriptor.
 *
 * @param fd file descriptor
 */
void fs_close(fs_fd_t fd);

#endif
