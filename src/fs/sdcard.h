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

#ifndef _SDCARD_H
#define _SDCARD_H

#include <stdint.h>

#include "constants.h"

extern void __fastcall__ sdinit(void);
extern void __fastcall__ sdpulse(void);
extern uint8_t __fastcall__ sdcmd00(void);
extern uint8_t __fastcall__ sdcmd08(uint8_t *resptr);
extern uint8_t __fastcall__ sdacmd41(void);
extern uint8_t __fastcall__ sdcmd58(uint8_t *resptr);
extern uint16_t __cdecl__ sdcmd17(uint32_t addr);

/*
 * Note: public fs APIs are resident wrappers that switch ROM to BANK 2
 * for execution of SD/FAT logic, then restore the previous ROM bank.
 */

/**
 * @brief Read one SD card sector.
 *
 * @param addr Logical block address (sector).
 * @return 0 on success.
 */
uint8_t read_sector(uint32_t addr);

/**
 * @brief Initialize and validate SD card state.
 *
 * @return 0 on success.
 */
uint8_t boot_sd(void);

/**
 * @brief Configure whether SD CRC mismatches should be ignored.
 *
 * @param enabled 0 to enforce CRC, non-zero to ignore CRC mismatch.
 */
void sdcard_set_ignore_crc(uint8_t enabled);

#endif
