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

#pragma once

#include <iostream>
#include <stdint.h>
#include <fstream>
#include <iostream>
#include <cstring>
#include <memory>
#include <string>

#include "cpu.h"
#include "acia.h"

/**
 * @brief ByteCradle Board Emulator - Base Class
 * 
 */
class ByteCradleBoard : public cpp65::Bus {
public:
    /**
     * @brief Construct a new Byte Cradle Board object
     * 
     */
    ByteCradleBoard();

    /**
     * @brief Destroy the Byte Cradle Board object
     * 
     */
    virtual ~ByteCradleBoard();
    
    /**
     * @brief Progresses the CPU by one tick
     * 
     */
    inline void tick() {
        this->cpu.tick();
    }

    /**
     * @brief Resets the CPU, clearing the registers and setting the program 
     *        counter to 0xFFFC; note that the CPU always needs to be reset
     *        before it can be used.
     * 
     */
    inline void reset() {
        cpp65::Bus::reset();
        this->cpu.tick();
    }

    /**
     * @brief Receives a keypress from the keyboard and places it into the key
     *        buffer
     *
     * @param ch 
     */
    inline void keypress(char ch) {
        this->acia->keypress(ch);
    }

    inline auto& get_acia() { return this->acia; }

    /**
     * @brief Returns true when the emulator should stop execution.
     */
    inline bool should_stop() const { return this->stop_requested; }

    /**
     * @brief Returns true when warnings are promoted to fatal errors.
     */
    inline bool are_warnings_fatal() const { return this->warnings_as_errors; }

protected:
    std::unique_ptr<ACIA> acia;     // Pointer to the ACIA object

    cpp65::CPU cpu;

    bool debug_mode = false;
    bool warnings_as_errors = false;
    bool stop_requested = false;

    inline auto& get_keybuffer() { return this->acia->get_keybuffer(); }
    /**
     * @brief Emit a warning exception event.
     *
     * By default this logs a warning. When warnings_as_errors is enabled, this
     * is promoted to a fatal error and marks the emulator for shutdown.
     */
    void warning_exception(const std::string& message);

    /**
     * @brief Load a file into memory (typically ROM)
     * 
     * @param filename path to the file
     * @param memory pointer to the memory buffer
     * @param sz size of the memory buffer
     * @return true if successful, false otherwise
     */
    bool load_file_into_memory(const char* filename, uint8_t* memory, size_t sz);
};
