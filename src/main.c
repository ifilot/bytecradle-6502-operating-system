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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fs/fat32.h"
#include "io.h"
#include "ramtest.h"
#include "command.h"

int main(void) {
    putstrnl("Starting system.");
    putstr("Clearing user space...   ");
    memset((void*)0x0800, 0xFF, 0x7700);
    putstrnl("[OK]");

    putstr("Mounting filesystem...   ");
    if(fs_mount() != 0x00) {
        putstrnl("[FAIL]");
        putstrnl("Cannot mount filesystem, exiting...");
        return -1;
    }
    putstrnl("[OK]");

    // put system in infinite loop
    command_pwdcmd();
    while(1){
        command_loop();
    }

    return 0;
}
