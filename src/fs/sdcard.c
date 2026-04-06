#include "sdcard.h"
#include "io.h"
#include "crc16.h"

#define SD_ROM_BANK 2

static uint8_t fs_boot_sd(void);
static uint8_t fs_read_sector(uint32_t addr);

/**
 * @brief Switch to the filesystem ROM bank and return previous bank.
 *
 * @return Previously active ROM bank number.
 */
static uint8_t fs_bank_enter(void) {
    uint8_t prev = get_rombank();
    set_rombank(SD_ROM_BANK);
    return prev;
}

/**
 * @brief Restore the previously active ROM bank.
 *
 * @param prev ROM bank to restore.
 */
static void fs_bank_exit(uint8_t prev) {
    set_rombank(prev);
}

/**
 * @brief Initialize and validate the SD card interface.
 *
 * This is a public wrapper that switches to BANK2, runs the SD init flow,
 * and restores the original bank before returning.
 *
 * @return 0 on success, 0xFF on error.
 */
uint8_t boot_sd(void) {
    uint8_t prev = fs_bank_enter();
    uint8_t ret = fs_boot_sd();
    fs_bank_exit(prev);
    return ret;
}

/**
 * @brief Read one 512-byte SD sector and verify CRC.
 *
 * This is a public wrapper that switches to BANK2, executes the low-level
 * sector read, and restores the original bank.
 *
 * @param addr Logical block address (sector).
 * @return 0 on success; halts on checksum mismatch.
 */
uint8_t read_sector(uint32_t addr) {
    uint8_t prev = fs_bank_enter();
    uint8_t ret = fs_read_sector(addr);
    fs_bank_exit(prev);
    return ret;
}

#pragma code-name(push, "B2CODE")
#pragma rodata-name(push, "B2RODATA")

/**
 * @brief Internal BANK2 SD boot sequence.
 *
 * @return 0 on success, 0xFF on error.
 */
static uint8_t fs_boot_sd(void) {
    uint8_t res;
    uint8_t resparr[5];

    sdinit();
    sdpulse();
    
    res = sdcmd00();
    if(res != 0x01) {
        putstr("[ERROR] Failed on CMD00: ");
        puthex(res);
        putcrlf();
        return 0xFF;
    }
    
    res = sdcmd08(resparr);
    if(res != 0x01) {
        putstr("[ERROR] Failed on CMD08: ");
        puthex(res);
        putcrlf();
        return 0xFF;
    }
    
    res = sdacmd41();
    if(res != 0x00) {
        putstr("[ERROR] Failed on ACMD41: ");
        puthex(res);
        putcrlf();
        return 0xFF;
    }

    res = sdcmd58(resparr);
    if(res != 0x00) {
        putstr("[ERROR] Failed on CMD58: ");
        puthex(res);
        putcrlf();
        return 0xFF;
    }

    if(resparr[1] != 0xC0) {
        putstr("[ERROR] CMD58: not a valid SD card: ");
        puthex(resparr[1]);
        putcrlf();
        return 0xFF;
    }

    return 0;
}

/**
 * @brief Internal BANK2 sector read with CRC validation.
 *
 * @param addr Logical block address (sector).
 * @return 0 on successful read/CRC match.
 */
static uint8_t fs_read_sector(uint32_t addr) {
    uint16_t checksum;
    uint16_t crc16;

    checksum = sdcmd17(addr);
    crc16 = crc16_xmodem_sdsector();

    if(checksum == crc16) {
        return 0;
    } else {
        putstrnl("[ERROR] SD-CARD checksum error: HALTING");
        puthex(checksum >> 8);
        puthex(checksum);
        putch(' ');
        puthex(crc16 >> 8);
        puthex(crc16);
        putcrlf();
        asm("loop: jmp loop");
    }
}

#pragma rodata-name(pop)
#pragma code-name(pop)
