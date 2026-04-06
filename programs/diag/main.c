#include <stdint.h>
#include <stdio.h>

#include "io.h"

#define RAM_BANK_REGISTER (*(volatile uint8_t*)0x7FC0u)
#define BANKED_RAM_BASE ((volatile uint8_t*)0x8000u)
#define BANKED_RAM_SIZE 0x2000u
#define FIRST_SAFE_RAM_BANK 4u
#define LAST_RAM_BANK 63u

typedef struct {
    uint8_t bank;
    uint16_t offset;
    uint8_t expected;
    uint8_t actual;
} diag_failure_t;

static uint8_t test_bank_non_destructive(uint8_t bank, diag_failure_t* failure) {
    uint16_t addr;

    RAM_BANK_REGISTER = bank;

    for(addr = 0x8000u; addr != 0xA000u; ++addr) {
        volatile uint8_t* cell = (volatile uint8_t*)addr;
        uint16_t offset = (uint16_t)(addr - 0x8000u);
        uint8_t original = *cell;
        uint8_t pattern_a = (uint8_t)(0xA5u ^ (uint8_t)offset ^ bank);
        uint8_t pattern_b = (uint8_t)~pattern_a;

        *cell = pattern_a;
        if(*cell != pattern_a) {
            failure->bank = bank;
            failure->offset = offset;
            failure->expected = pattern_a;
            failure->actual = *cell;
            return 0u;
        }

        *cell = pattern_b;
        if(*cell != pattern_b) {
            failure->bank = bank;
            failure->offset = offset;
            failure->expected = pattern_b;
            failure->actual = *cell;
            return 0u;
        }

        *cell = original;
        if(*cell != original) {
            failure->bank = bank;
            failure->offset = offset;
            failure->expected = original;
            failure->actual = *cell;
            return 0u;
        }
    }

    return 1u;
}

int main() {
    uint8_t bank;
    uint8_t passed = 0u;
    uint8_t tested = 0u;
    uint8_t original_bank;
    char line[64];
    diag_failure_t failure;

    if((uint8_t)(bcos_abi_major() ^ BCOS_ABI_MAJOR) != 0u) {
        putstrnl("DIAG: ABI mismatch.");
        return 1;
    }

    original_bank = RAM_BANK_REGISTER;

    putstrnl("=== BYTECRADLE DIAGNOSTICS (DIAG) ===");
    putstrnl("Memory bank test (non-destructive).");
    sprintf(line, "Initial RAM bank: $%02X", original_bank);
    putstrnl(line);
    putstrnl("Testing writable banked RAM: $8000-$9FFF");
    sprintf(line, "Bank range: $%02X-$%02X (safe non-OS banks)", FIRST_SAFE_RAM_BANK, LAST_RAM_BANK);
    putstrnl(line);
    putstrnl("Patterns: A5^offset^bank, inverse, restore");

    for(bank = FIRST_SAFE_RAM_BANK; bank <= LAST_RAM_BANK; ++bank) {
        ++tested;
        sprintf(line, "[BANK %02u/$%02X] testing...", bank, bank);
        putstrnl(line);

        if(test_bank_non_destructive(bank, &failure) == 0u) {
            RAM_BANK_REGISTER = original_bank;
            putstrnl("RESULT: FAIL");
            sprintf(line, "Fail bank=$%02X offset=$%04X", failure.bank, failure.offset);
            putstrnl(line);
            sprintf(line, "Expected=$%02X Actual=$%02X", failure.expected, failure.actual);
            putstrnl(line);
            return 2;
        }

        ++passed;
        putstrnl(" -> PASS");
    }

    RAM_BANK_REGISTER = original_bank;

    putstrnl("--------------------------------------");
    sprintf(line, "SUMMARY: PASS (%u/%u banks)", passed, tested);
    putstrnl(line);
    sprintf(line, "RAM bank restored to $%02X", original_bank);
    putstrnl(line);

    return 0;
}
