#include <stdint.h>

#define BCOS_ARGC_ADDR (*(volatile uint8_t*)0x0600u)
#define BCOS_ARGV_ADDR ((char**)0x0601u)

extern int main(int argc, char** argv);

int callmain(void) {
    return main((int)BCOS_ARGC_ADDR, BCOS_ARGV_ADDR);
}
