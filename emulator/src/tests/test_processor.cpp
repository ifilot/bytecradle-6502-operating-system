#include <catch2/catch_test_macros.hpp>

#include "bus.h"
#include "cpu.h"

namespace {

struct CpuHarness {
    cpp65::RamBus bus;
    cpp65::CPU cpu;

    CpuHarness() : cpu(bus, cpp65::CPUModel::wdc65c02) {
        for (uint32_t addr = 0; addr <= 0xFFFF; ++addr) {
            bus.poke(static_cast<uint16_t>(addr), 0xEA); // NOP
        }
    }

    void reset_to(uint16_t addr) {
        bus.set_reset_vector(addr);
        bus.reset();
        cpu.tick();
    }

    void run_ticks(size_t count) {
        for (size_t i = 0; i < count; ++i) {
            cpu.tick();
        }
    }
};

} // namespace

TEST_CASE("cpp65 CPU executes load, arithmetic, and store instructions", "[cpu]") {
    CpuHarness h;

    h.bus.load(0x8000, {
        0xA9, 0x50,       // LDA #$50
        0x18,             // CLC
        0x69, 0x50,       // ADC #$50
        0x85, 0x20,       // STA $20
    });

    h.reset_to(0x8000);
    h.run_ticks(2 + 2 + 2 + 3);

    REQUIRE(h.bus.peek(0x0020) == 0xA0);
}

TEST_CASE("cpp65 CPU executes JSR and RTS with correct flow", "[cpu]") {
    CpuHarness h;

    h.bus.load(0x8000, {
        0x20, 0x00, 0x90, // JSR $9000
        0xA9, 0x11,       // LDA #$11
        0x85, 0x20,       // STA $20
    });

    h.bus.load(0x9000, {
        0xA9, 0x42,       // LDA #$42
        0x85, 0x21,       // STA $21
        0x60,             // RTS
    });

    h.reset_to(0x8000);
    h.run_ticks(6 + 2 + 3 + 6 + 2 + 3);

    REQUIRE(h.bus.peek(0x0021) == 0x42);
    REQUIRE(h.bus.peek(0x0020) == 0x11);
}

TEST_CASE("cpp65 CPU stores and loads through the bus", "[cpu]") {
    CpuHarness h;

    h.bus.load(0x8000, {
        0xA9, 0x3C,       // LDA #$3C
        0x85, 0x20,       // STA $20
        0xA9, 0x00,       // LDA #$00
        0xA5, 0x20,       // LDA $20
        0x85, 0x21,       // STA $21
    });

    h.reset_to(0x8000);
    h.run_ticks(2 + 3 + 2 + 3 + 3);

    REQUIRE(h.bus.peek(0x0020) == 0x3C);
    REQUIRE(h.bus.peek(0x0021) == 0x3C);
}
