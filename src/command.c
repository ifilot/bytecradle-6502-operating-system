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
#include "version.h"
#include "crc16.h"

static char command_buffer[40];
static char* command_argv[20];
static uint8_t command_argc;
static uint8_t command_parse_too_many_args;
static char* command_ptr = command_buffer;

/*
 * Argument list (argv / argc) position
 */
#define BCOS_ARGC_ADDR       ((uint8_t*)0x0600)
#define BCOS_ARGV_TABLE_ADDR ((char**)0x0601)
#define BCOS_ARGV_STR_ADDR   ((char*)0x0630)
#define BCOS_ARGV_STR_END    ((char*)0x067F)

/**
 * Header of .COM files
 */
typedef struct {
    uint16_t load_addr;     // deployment address
    uint16_t payload_len;   // program length (excluding header)
    uint8_t abi_major;      // ABI major version (error on mismatch)
    uint8_t abi_minor;      // ABI minor version (warning on mismatch)
    uint16_t payload_crc;   // payload CRC16/XMODEM checksum
} ComHeader;

/*
 * List of system commands
 */
static const CommandEntry command_table[] = {
    { "LS", command_ls },
    { "DIR", command_ls },
    { "CD", command_cd },
    { "MORE", command_more },
    { "HEXDUMP", command_hexdump },
    { "SDINFO", command_sdinfo },
    { "VERSION", command_version },
};

// forward declarations
static uint8_t command_is_probably_binary(const uint8_t *data, uint32_t size);

/**
 * @brief Continuously sample keyboard input, retrieve and parse commands
 */
void command_loop() {
    uint8_t c;
    const char *command_end = command_buffer + sizeof(command_buffer) - 1;

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
                    if(command_ptr < command_end) {
                        *(command_ptr++) = c;
                        putch(c);
                    } else {
                        putch('\a');
                    }
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
    if(command_parse_too_many_args != 0) {
        putstrnl("Too many arguments");
        command_ptr = command_buffer;
        memset(command_buffer, 0x00, sizeof(command_buffer));
        command_pwdcmd();
        return;
    }

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
    memset(command_buffer, 0x00, sizeof(command_buffer));
    command_pwdcmd();
}

/**
 * @brief Parse command buffer, splits using spaces
 */
void command_parse() {
    char* ptr = strtok(command_buffer, " ");
    uint8_t i;

    command_argc = 0;
    command_parse_too_many_args = 0;

    while(ptr != NULL) {
        if(command_argc >= (sizeof(command_argv) / sizeof(command_argv[0]) - 1)) {
            command_parse_too_many_args = 1;
            break;
        }
        command_argv[command_argc++] = ptr;
        ptr = strtok(NULL, " ");
    }
    command_argv[command_argc] = NULL;

    for(i=0; i<command_argc; i++) {
        if(i == 0) {
            strupper(command_argv[i]);
        }
    }
}

/**
 * @brief Copy parsed command line arguments to a fixed RAM location.
 */
static void command_store_program_args() {
    uint8_t i;
    uint8_t j;
    uint8_t argc = command_argc;
    char* strptr = BCOS_ARGV_STR_ADDR;
    char** argv = BCOS_ARGV_TABLE_ADDR;

    if(argc > (sizeof(command_argv) / sizeof(command_argv[0]) - 1)) {
        argc = sizeof(command_argv) / sizeof(command_argv[0]) - 1;
    }

    *BCOS_ARGC_ADDR = argc;

    for(i=0; i<argc; i++) {
        argv[i] = strptr;
        j = 0;
        while(command_argv[i][j] != '\0') {
            if(strptr >= BCOS_ARGV_STR_END) {
                *(BCOS_ARGC_ADDR) = i;
                *strptr = '\0';
                argv[i] = NULL;
                argv[i + 1] = NULL;
                return;
            }
            *(strptr++) = command_argv[i][j++];
        }
        if(strptr >= BCOS_ARGV_STR_END) {
            *(BCOS_ARGC_ADDR) = i + 1;
            *(BCOS_ARGV_STR_END) = '\0';
            argv[i + 1] = NULL;
            return;
        }
        *(strptr++) = '\0';
    }

    argv[argc] = NULL;
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
    uint8_t column = 0;
    uint8_t c;

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

    if(command_is_probably_binary(ptr, filesize)) {
        putstrnl("Binary file detected. Use HEXDUMP.");
        return;
    }

    while(filesize-- > 0) {
        c = *(ptr++);

        if(c == '\r') {
            continue;
        }

        if(c == '\n') {
            putcrlf();
            column = 0;
            linecounter++;
        } else if(c == '\t') {
            do {
                putch(' ');
                column++;

                if(column == 80) {
                    putcrlf();
                    column = 0;
                    linecounter++;
                }
            } while((column & 0x07) != 0);
        } else {
            if(c < ' ' || c > '~') {
                c = '.';
            }

            putch(c);
            column++;

            if(column == 80) {
                putcrlf();
                column = 0;
                linecounter++;
            }
        }

        if(linecounter >= 20) {
            putstrnl("--- SPACE: next page, Q: quit ---");
            do {
                c = getch();
                if(c == 'q' || c == 'Q') {
                    putstrnl("");
                    return;
                }
            } while(c != ' ');
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
    ComHeader hdr;
    uint16_t calc_crc;
    uint32_t filesize = 0;
    uint16_t payload_size;
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
    if(fs_read(fd, (uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr)) {
        fs_close(fd);
        return 0xFF;
    }
    fs_close(fd);

    if(hdr.load_addr < 0x0800 || hdr.load_addr >= 0x7F00) {
        putstrnl("Invalid start address: file corrupt?");
        return 0xFF;
    }

    if(hdr.payload_len == 0) {
        putstrnl("Invalid payload length");
        return 0xFF;
    }

    if(fs_load_file(filename, (uint8_t*)hdr.load_addr, &filesize) != 0x00) {
        putstrnl("Cannot load file");
        return 0xFF;
    }

    if(filesize < COM_HEADER_SIZE) {
        putstrnl("Invalid COM header");
        return 0xFF;
    }

    payload_size = (uint16_t)(filesize - COM_HEADER_SIZE);
    if(hdr.payload_len != payload_size) {
        putstrnl("Payload length mismatch");
        return 0xFF;
    }

    if(((uint32_t)hdr.load_addr) + filesize >= 0x7F00) {
        putstrnl("File too large to fit in memory");
        return 0xFF;
    }

    if(hdr.abi_major != BCOS_ABI_MAJOR) {
        putstrnl("Incompatible ABI major version");
        return 0xFF;
    }

    if(hdr.abi_minor != BCOS_ABI_MINOR) {
        putstr("Warning: ABI minor mismatch. Program=");
        puthex(hdr.abi_minor);
        putstr(" OS=");
        puthex(BCOS_ABI_MINOR);
        putcrlf();
    }

    calc_crc = crc16_xmodem(((uint8_t*)hdr.load_addr) + COM_HEADER_SIZE, payload_size);
    if(calc_crc != hdr.payload_crc) {
        putstrnl("Payload checksum failed");
        return 0xFF;
    }

    command_store_program_args();

    program = (void(*)(void))(hdr.load_addr + COM_HEADER_SIZE);
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
    uint16_t i = 0;
    uint8_t linebuf[16];
    uint8_t c;
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

    for(i = 0; i < 256; i++) {
        if(i % 16 == 0) {
            puthexword(addr);
            putstr(": ");
        }
        c = *(uint8_t*)addr;
        linebuf[i & 0x0F] = c;
        puthex(c);
        addr++;
        putspace();

        if(i % 16 == 15) {
            uint8_t j;

            putstr(" | ");
            for(j = 0; j < 16; j++) {
                c = linebuf[j];
                if(c >= 32 && c <= 127) {
                    putch(c);
                } else {
                    putch('.');
                }
            }
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

/**
 * @brief Outputs operating system version information
 *
 */
void command_version() {
    version_print();
}

/**
 * @brief Heuristic check for binary data in a text viewer.
 *
 * Rule of thumb:
 * - Immediate binary if NUL byte is present.
 * - Otherwise, if >10% of sampled bytes are unusual control characters,
 *   classify as binary.
 */
static uint8_t command_is_probably_binary(const uint8_t *data, uint32_t size) {
    uint32_t sample = size > 512 ? 512 : size;
    uint32_t i;
    uint16_t suspicious = 0;
    uint8_t c;

    for(i = 0; i < sample; i++) {
        c = data[i];

        if(c == 0x00) {
            return 1;
        }

        if(c < 0x20 &&
           c != '\t' &&
           c != '\n' &&
           c != '\r' &&
           c != '\f' &&
           c != 0x1B) {
            suspicious++;
        }

        if(c == 0x7F) {
            suspicious++;
        }
    }

    if(sample == 0) {
        return 0;
    }

    return (uint8_t)((uint32_t)suspicious * 10 > sample);
}
