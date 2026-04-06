#include <stdint.h>
#include <stdio.h>

#include "io.h"

int main() {
    uint8_t i;
    uint8_t argc = *(uint8_t*)BCOS_ARGC_ADDR;
    char** argv = (char**)BCOS_ARGV_ADDR;
    char buf[8];

    if((uint8_t)(bcos_abi_major() ^ BCOS_ABI_MAJOR) != 0u) {
        return 1;
    }

    sprintf(buf, "%u", (unsigned int)argc);
    putstr("ARGC=");
    putstrnl(buf);

    for(i = 0; i < argc; i++) {
        putstr("ARGV[");
        sprintf(buf, "%u", (unsigned int)i);
        putstr(buf);
        putstr("]=");
        putstrnl(argv[i]);
    }

    return 0;
}
