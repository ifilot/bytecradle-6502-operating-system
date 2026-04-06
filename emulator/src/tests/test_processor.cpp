#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "vrEmu6502/vrEmu6502.h"

namespace {

uint8_t mem_read(VrEmu6502* cpu, uint16_t addr, bool) {
    auto* mem = static_cast<std::array<uint8_t, 65536>*>(vrEmu6502GetUserData(cpu));
    return (*mem)[addr];
}

void mem_write(VrEmu6502* cpu, uint16_t addr, uint8_t val) {
    auto* mem = static_cast<std::array<uint8_t, 65536>*>(vrEmu6502GetUserData(cpu));
    (*mem)[addr] = val;
}

struct CpuHarness {
    std::array<uint8_t, 65536> mem{};
    VrEmu6502* cpu = nullptr;

    CpuHarness() {
        mem.fill(0xEA); // NOP
        cpu = vrEmu6502New(CPU_W65C02, mem_read, mem_write, &mem);
    }

    ~CpuHarness() {
        vrEmu6502Destroy(cpu);
    }

    void set_reset_vector(uint16_t addr) {
        mem[0xFFFC] = static_cast<uint8_t>(addr & 0xFF);
        mem[0xFFFD] = static_cast<uint8_t>((addr >> 8) & 0xFF);
    }

    void reset_to(uint16_t addr) {
        set_reset_vector(addr);
        vrEmu6502Reset(cpu);
    }

    void run_instruction() {
        (void)vrEmu6502InstCycle(cpu);
    }
};

} // namespace

TEST_CASE("CPU executes load and arithmetic instructions with expected flags", "[cpu]") {
    CpuHarness h;

    // 0x8000: LDA #$50
    // 0x8002: CLC
    // 0x8003: ADC #$50 => 0xA0 with overflow set
    h.mem[0x8000] = 0xA9;
    h.mem[0x8001] = 0x50;
    h.mem[0x8002] = 0x18;
    h.mem[0x8003] = 0x69;
    h.mem[0x8004] = 0x50;

    h.reset_to(0x8000);

    h.run_instruction();
    REQUIRE(vrEmu6502GetAcc(h.cpu) == 0x50);

    h.run_instruction();
    REQUIRE((vrEmu6502GetStatus(h.cpu) & FlagC) == 0);

    h.run_instruction();
    REQUIRE(vrEmu6502GetAcc(h.cpu) == 0xA0);
    REQUIRE((vrEmu6502GetStatus(h.cpu) & FlagV) != 0);
    REQUIRE((vrEmu6502GetStatus(h.cpu) & FlagN) != 0);
    REQUIRE((vrEmu6502GetStatus(h.cpu) & FlagZ) == 0);
}

TEST_CASE("CPU executes JSR and RTS with correct stack and flow", "[cpu]") {
    CpuHarness h;

    // Main at 0x8000
    // 0x8000: JSR $9000
    // 0x8003: LDA #$11
    h.mem[0x8000] = 0x20;
    h.mem[0x8001] = 0x00;
    h.mem[0x8002] = 0x90;
    h.mem[0x8003] = 0xA9;
    h.mem[0x8004] = 0x11;

    // Subroutine at 0x9000
    // 0x9000: LDA #$42
    // 0x9002: RTS
    h.mem[0x9000] = 0xA9;
    h.mem[0x9001] = 0x42;
    h.mem[0x9002] = 0x60;

    h.reset_to(0x8000);
    uint8_t sp_before = vrEmu6502GetStackPointer(h.cpu);

    h.run_instruction(); // JSR
    REQUIRE(vrEmu6502GetPC(h.cpu) == 0x9000);
    REQUIRE(vrEmu6502GetStackPointer(h.cpu) == static_cast<uint8_t>(sp_before - 2));

    h.run_instruction(); // LDA #$42
    REQUIRE(vrEmu6502GetAcc(h.cpu) == 0x42);

    h.run_instruction(); // RTS
    REQUIRE(vrEmu6502GetPC(h.cpu) == 0x8003);
    REQUIRE(vrEmu6502GetStackPointer(h.cpu) == sp_before);

    h.run_instruction(); // LDA #$11
    REQUIRE(vrEmu6502GetAcc(h.cpu) == 0x11);
}

TEST_CASE("CPU stores and loads through memory callbacks", "[cpu]") {
    CpuHarness h;

    // 0x8000: LDA #$3C
    // 0x8002: STA $20
    // 0x8004: LDA #$00
    // 0x8006: LDA $20
    h.mem[0x8000] = 0xA9;
    h.mem[0x8001] = 0x3C;
    h.mem[0x8002] = 0x85;
    h.mem[0x8003] = 0x20;
    h.mem[0x8004] = 0xA9;
    h.mem[0x8005] = 0x00;
    h.mem[0x8006] = 0xA5;
    h.mem[0x8007] = 0x20;

    h.reset_to(0x8000);

    h.run_instruction();
    h.run_instruction();
    REQUIRE(h.mem[0x0020] == 0x3C);

    h.run_instruction();
    REQUIRE(vrEmu6502GetAcc(h.cpu) == 0x00);

    h.run_instruction();
    REQUIRE(vrEmu6502GetAcc(h.cpu) == 0x3C);
}
