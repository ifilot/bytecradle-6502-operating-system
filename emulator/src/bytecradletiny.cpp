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

#include "bytecradletiny.h"
#include <cstdio>

/**
 * @brief Construct a new ByteCradleTiny object
 * 
 * @param romfile path to the ROM file
 * @param debug_mode whether to run in debugging mode
 */
ByteCradleTiny::ByteCradleTiny(const std::string& romfile,
                               bool _debug_mode,
                               bool _warnings_as_errors) {
    debug_mode = _debug_mode;
    warnings_as_errors = _warnings_as_errors;

    // set memory
    memset(this->ram, 0x00, sizeof(this->ram));
    this->load_file_into_memory(romfile.c_str(), this->rom, sizeof(this->rom));

    // set interface chips
    this->acia = std::make_unique<ACIA>(ByteCradleTiny::ACIA_MASK, 
                                        ByteCradleTiny::ACIA_MASK_SIZE, 
                                        this);
}

/**
 * @brief Destroy the Byte Cradle Tiny object
 * 
 */
ByteCradleTiny::~ByteCradleTiny() {
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
uint8_t ByteCradleTiny::read(uint16_t addr) {
    // ROM chip
    if(addr >= 0x8000) {
        return this->rom[addr - 0x8000];
    }

    // lower RAM
    if (addr < 0x7F00) {
        return this->ram[addr];
    }

    if(this->get_acia()->responds(addr)) {
        return this->get_acia()->read(addr);
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
void ByteCradleTiny::write(uint16_t addr, uint8_t val) {
    // store in lower memory
    if (addr < 0x7F00) {
        this->ram[addr] = val;
        return;
    }

    if(this->get_acia()->responds(addr)) {
        this->get_acia()->write(addr, val);
        return;
    }

    if(addr == 0x7F00) {
        // control line 1; does not require any output
        return;
    }

    if(addr == 0x7F01) {
        // control line 2; does not require any output
        return;
    }

    if (addr >= 0x8000) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "Write to read-only ROM at $%04X.", addr);
        this->warning_exception(buffer);
        return;
    }

    printf("[ERROR] Invalid write: %04X.\n", addr);
}
