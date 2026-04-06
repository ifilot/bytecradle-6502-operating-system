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

#include "version.h"
#include <stdint.h>
#include "io.h"

static const char bcos_version[] = BCOS_VERSION;
static const char bcos_build_date[] = __DATE__;
static const char bcos_build_time[] = __TIME__;

const char* version_get_string(void) {
    return bcos_version;
}

const char* version_get_build_date(void) {
    return bcos_build_date;
}

const char* version_get_build_time(void) {
    return bcos_build_time;
}

void version_print(void) {
    putstr("Version: ");
    putstrnl(version_get_string());
    putstr("Build date: ");
    putstr(version_get_build_date());
    putstr(" ");
    putstrnl(version_get_build_time());
}
