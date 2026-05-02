/**************************************************************************
 *                                                                        *
 *   Author: Ivo Filot <ivo@ivofilot.nl>                                  *
 *                                                                        *
 *   ByteCradle 6502 EMULATOR is free software:                           *
 *   you can redistribute it and/or modify it under the terms of the      *
 *   GNU General Public License as published by the Free Software         *
 *   Foundation, either version 3 of the License, or (at your option)     *
 *   any later version.                                                   *
 *                                                                        *
 *   This program is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty          *
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.              *
 *   See the GNU General Public License for more details.                 *
 *                                                                        *
 *   You should have received a copy of the GNU General Public License    *
 *   along with this program.  If not, see http://www.gnu.org/licenses/.  *
 *                                                                        *
 **************************************************************************/

#include "bytecradlemini.h"
#include <cstdio>

/**
 * @brief Construct a new ByteCradleMini object
 * 
 * @param romfile path to the ROM file
 * @param sdcardfile path to the SD card file
 * @param debug_mode whether to run in debugging mode
 */
ByteCradleMini::ByteCradleMini(const std::string& romfile, 
                               const std::string& sdcardfile,
                               bool _sdcard_read_only,
                               bool _debug_mode,
                               bool _warnings_as_errors) {
    debug_mode = _debug_mode;
    warnings_as_errors = _warnings_as_errors;

    // set memory
    memset(this->ram, 0x00, sizeof(this->ram));
    this->load_file_into_memory(romfile.c_str(), this->rom, sizeof(this->rom));

    // set interface chips
    this->acia = std::make_unique<ACIA>(ByteCradleMini::ACIA_MASK, 
                                        ByteCradleMini::ACIA_MASK_SIZE, 
                                        this);
    this->via = std::make_unique<VIA>(ByteCradleMini::VIA_MASK, 
                                      ByteCradleMini::VIA_MASK_SIZE);
    this->via->create_sdcard_and_attach(sdcardfile, this->debug_mode, _sdcard_read_only);
}

/**
 * @brief Destroy the Byte Cradle Tiny object
 * 
 */
ByteCradleMini::~ByteCradleMini() {
    // Destructor will call base class destructor and clean cpu
}

/**
 * @brief Read memory function
 * 
 * This function also needs to handle any memory mapped I/O devices
 * 
 * @param addr memory address
 * @param isDbg 
 * @return uint8_t value at memory address
 */
uint8_t ByteCradleMini::read(uint16_t addr) {
    // ROM chip
    if(addr >= 0xC000) {
        return this->rom[addr - 0xC000];
    }

    // lower RAM
    if (addr < 0x7F00) {
        return this->ram[addr];
    }

    // RAM banked region: $8000–$9FFF
    if (addr >= 0x8000 && addr <= 0x9FFF) {
        unsigned int rambank = this->get_rambank();
        return this->ram[(addr - 0x8000) | rambank << 13];
    }
    
    // ROM banked region: $A000–$BFFF
    if (addr >= 0xA000 && addr <= 0xBFFF) {
        unsigned int rombank = this->get_rombank();
        return this->rom[(addr - 0xA000) | rombank << 13];
    }

    // ACIA chip
    if(this->get_acia()->responds(addr)) {
        return this->get_acia()->read(addr);
    }
    
    // VIA chip
    if(this->get_via()->responds(addr)) {
        return this->get_via()->read(addr);
    }

    // retrieve current rombank
    if ((addr & ByteCradleMini::BANKMASK) == ROMBANK_PATTERN) {
        return this->get_rombank();
    }

    // retrieve current rambank
    if ((addr & ByteCradleMini::BANKMASK) == RAMBANK_PATTERN) {
        return this->get_rambank();
    }

    printf("[ERROR] Invalid write: %04X.\n", addr);

    return 0xFF;
}

/**
 * @brief Write to memory function
 * 
 * @param addr memory address
 * @param val value to write
 */
void ByteCradleMini::write(uint16_t addr, uint8_t val) {
    // store in lower memory
    if (addr < 0x7F00) {
        this->ram[addr] = val;
        return;
    }

    // RAM banked region: $8000–$9FFF
    if (addr >= 0x8000 && addr <= 0x9FFF) {
        unsigned int rambank = this->get_rambank();
        this->ram[(addr - 0x8000) | rambank << 13] = val;
        return;
    }

    // retrieve current rombank
    if ((addr & ByteCradleMini::BANKMASK) == ROMBANK_PATTERN) {
        this->set_rombank(val);
        return;
    }

    // retrieve current rambank
    if ((addr & ByteCradleMini::BANKMASK) == RAMBANK_PATTERN) {
        this->set_rambank(val);
        return;
    }

    // ACIA chip
    if(this->get_acia()->responds(addr)) {
        this->get_acia()->write(addr, val);
        return;
    }

    // VIA chip
    if(this->get_via()->responds(addr)) {
        this->get_via()->write(addr, val);
        return;
    }

    if ((addr >= 0xA000 && addr <= 0xBFFF) || addr >= 0xC000) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "Write to read-only ROM at $%04X.", addr);
        this->warning_exception(buffer);
        return;
    }

    printf("[ERROR] Invalid write: %04X.\n", addr);

}
