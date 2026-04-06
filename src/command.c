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

#include "command.h"

static char command_buffer[40];
static char* command_argv[20];
static uint8_t command_argc;
static char* command_ptr = command_buffer;

static const CommandEntry command_table[] = {
    { "LS", command_ls },
    { "DIR", command_ls },
    { "CD", command_cd },
    { "MORE", command_more },
    { "HEXDUMP", command_hexdump },
    { "SDINFO", command_sdinfo },
};

/**
 * @brief Continuously sample keyboard input, retrieve and parse commands
 */
void command_loop() {
    uint8_t c;

    c = getch();
    if(c != 0) {
        switch(c) {
            case 0x0D:
                command_exec();
            break;
            case 0x7F:
                if(command_ptr > command_buffer) {
                    putbackspace();
                    command_ptr--;
                }
            break;
            default:
                if(c >= ' ' && c <= '~') {
                    *(command_ptr++) = c;
                    putch(c);
                }
            break;
        }
    }
}

/**
 * @brief Try to execute a command when user has pressed enter
 */
void command_exec() {
    uint8_t i = 0;

    putstrnl("");
    *command_ptr = 0x00;    // place terminating byte
    command_parse();        // split string

    if(command_argc == 0) {
        command_pwdcmd();
        return;
    }

    // try to see if we can find a match
    for(i=0; i<(sizeof(command_table) / sizeof(command_table[0])); i++) {
        if(strcmp(command_table[i].str, command_argv[0]) == 0) {
            command_table[i].func();
            break;
        }
    }

    // throw illegal command
    if(i == (sizeof(command_table) / sizeof(command_table[0]))) {
        if(!command_try_com() == 0) {
            command_illegal();
        }
    }

    command_ptr = command_buffer;
    memset(command_buffer, 0x00, strlen(command_buffer));
    command_pwdcmd();
}

/**
 * @brief Parse command buffer, splits using spaces
 */
void command_parse() {
    char* ptr = strtok(command_buffer, " ");
    char** argv_ptr = command_argv;
    uint8_t i;

    *(argv_ptr++) = ptr;
    command_argc = 0;

    while(ptr != NULL) {
        ptr = strtok(NULL, " ");
        *(argv_ptr++) = ptr;
        command_argc++;
    }

    for(i=0; i<command_argc; i++) {
        strupper(command_argv[i]);
    }
}

/**
 * @brief Execute the "ls" command
 */
void command_ls() {
    // ensure proper RAM bank
    asm("lda #63");
    asm("sta %w", RAMBANKREGISTER);
    
    fs_list_dir();
}

/**
 * @brief Execute the "cd" command
 */
void command_cd() {
    // ensure proper RAM bank
    asm("lda #63");
    asm("sta %w", RAMBANKREGISTER);

    if(command_argc == 1) {
        fs_chdir("/");
        return;
    }

    if(command_argc != 2) {
        command_illegal();
        return;
    }

    if(fs_chdir(command_argv[1]) != 0x00) {
        putstrnl("Cannot find folder");
    }
}

/**
 * @brief Places the current working directory on the screen
 */
void command_pwdcmd() {
    char buf[80];

    // ensure proper RAM bank
    asm("lda #63");
    asm("sta %w", RAMBANKREGISTER);

    if(fs_getcwd(buf, sizeof(buf)) == 0x00) {
        putstr(buf);
    } else {
        putstr(":/> ");
    }
}

/**
 * @brief Informs the user on an illegal command
 */
void command_illegal() {
    putstr("Illegal command: ");
    putstrnl(command_argv[0]);
}

/**
 * @brief Outputs file to screen assuming human-readable content
 */
void command_more() {
    const uint8_t *ptr;
    uint32_t filesize = 0;
    uint8_t linecounter = 0;
    uint8_t charcounter = 0;

    // ensure proper RAM bank
    asm("lda #63");
    asm("sta %w", RAMBANKREGISTER);

    if(command_argc != 2) {
        command_illegal();
        return;
    }

    if(fs_load_file(command_argv[1], (uint8_t*)(0x1000), &filesize) != 0x00) {
        putstrnl("Cannot find file.");
        return;
    }
    ptr = (uint8_t*)0x1000;
    while(filesize-- > 0) {
        if(*ptr == '\n') {
            ptr++;
            putcrlf();
            linecounter++;
        } else {
            putch(*(ptr++));
        }

        charcounter++;
        if(charcounter == 80) {
            putcrlf();
            charcounter = 0;
            linecounter++;
        }

        if(linecounter == 20) {
            putstrnl("--- Press SPACE to continue ---");
            while(getch() != ' ') {}
            linecounter = 0;
        }
    }
}

/**
 * @brief Try to launch a .COM file
 */
uint8_t command_try_com() {
    fs_fd_t fd;
    char filename[16] = {0};
    uint8_t l = strlen(command_argv[0]);
    uint16_t addr;
    uint8_t hdr[2];
    uint32_t filesize = 0;
    void __fastcall__ (*program)(void) = NULL;

    // ensure proper RAM bank
    asm("lda #63");
    asm("sta %w", RAMBANKREGISTER);

    l = l > 8 ? 8 : l;
    memcpy(filename, command_argv[0], l);
    memcpy(&filename[l], ".COM", 4);

    fd = fs_open(filename);
    if(fd < 0) {
        return 0xFF;
    }
    if(fs_read(fd, hdr, sizeof(hdr)) != sizeof(hdr)) {
        fs_close(fd);
        return 0xFF;
    }
    fs_close(fd);

    addr = *(uint16_t*)hdr;

    if(!(addr >= 0x0800 && addr < 0x7F00)) {
        putstrnl("Invalid start address: file corrupt?");
        return 0xFF;
    }

    if(fs_load_file(filename, (uint8_t*)addr, &filesize) != 0x00) {
        putstrnl("Cannot load file");
        return 0xFF;
    }
    if(addr + filesize >= 0x7F00) {
        putstrnl("File too large to fit in memory");
        return 0xFF;
    }

    program = (void(*)(void))(addr+2);
    program();
    memset((void*)0x0800, 0xFF, 0x7700);    // clean up user space
    return 0;
}

/**
 * @brief Dumps memory contents to screen
 * 
 */
void command_hexdump() {
    uint16_t addr;
    uint32_t parsed_addr;
    uint16_t i=0;
    char* endptr;

    if(command_argc != 2) {
        command_illegal();
        return;
    }

    // convert input to hex
    parsed_addr = strtoul(command_argv[1], &endptr, 16);

    if(*endptr != '\0' || parsed_addr > 0xFFFF) {
        putstr("Invalid address: ");
        putstrnl(command_argv[1]);
        return;
    }
    addr = (uint16_t)parsed_addr;

    for(i=0; i<256; i++) {
        if(i % 16 == 0) {
            puthexword(addr);
            putstr(": ");
        }
        puthex(*(uint8_t*)addr);
        addr++;
        putspace();
        if(i % 16 == 15) {
            putstrnl("");
        }
    }
}

/**
 * @brief Outputs SD-CARD information to screen
 * 
 */
void command_sdinfo() {
    fs_print_partition_info();
}
