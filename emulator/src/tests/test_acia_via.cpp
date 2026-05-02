#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "acia.h"
#include "via.h"

namespace {

class NullBus : public cpp65::Bus {
public:
    uint8_t read(uint16_t) override { return 0xFF; }
    void write(uint16_t, uint8_t) override {}
};

} // namespace

TEST_CASE("ACIA responds to addresses based on configured mask", "[acia]") {
    NullBus bus;
    ACIA acia(0xA000, 12, &bus);

    REQUIRE(acia.responds(0xA000));
    REQUIRE(acia.responds(0xA003));
    REQUIRE_FALSE(acia.responds(0xA100));
}

TEST_CASE("ACIA keypress updates status and reading data drains buffer", "[acia]") {
    NullBus bus;
    ACIA acia(0xA000, 12, &bus);

    acia.keypress('X');
    REQUIRE(bus.irq_asserted());
    REQUIRE(acia.read(ACIA_STAT) == 0x08);

    REQUIRE(acia.read(ACIA_DATA) == static_cast<uint8_t>('X'));
    REQUIRE_FALSE(bus.irq_asserted());
    REQUIRE(acia.read(ACIA_STAT) == 0x00);
}

TEST_CASE("ACIA converts LF to CR on read", "[acia]") {
    NullBus bus;
    ACIA acia(0xA000, 12, &bus);

    acia.keypress('\n');
    REQUIRE(acia.read(ACIA_DATA) == 0x0D);
}

TEST_CASE("ACIA command and control registers support write/read", "[acia]") {
    NullBus bus;
    ACIA acia(0xA000, 12, &bus);

    acia.write(ACIA_CMD, 0xA5);
    acia.write(ACIA_CTRL, 0x5A);

    REQUIRE(acia.read(ACIA_CMD) == 0xA5);
    REQUIRE(acia.read(ACIA_CTRL) == 0x5A);
}

TEST_CASE("ACIA data register write emits byte to stdout", "[acia]") {
    NullBus bus;
    ACIA acia(0xA000, 12, &bus);

    std::ostringstream captured;
    auto* old_buf = std::cout.rdbuf(captured.rdbuf());

    acia.write(ACIA_DATA, 'Z');

    std::cout.rdbuf(old_buf);

    REQUIRE(captured.str() == "Z");
}

TEST_CASE("ACIA keybuffer keeps the newest 16 characters", "[acia]") {
    NullBus bus;
    ACIA acia(0xA000, 12, &bus);

    for (int i = 0; i < 20; ++i) {
        acia.keypress(static_cast<char>('a' + i));
    }

    REQUIRE(acia.get_keybuffer().size() == 16);
    REQUIRE(acia.read(ACIA_DATA) == static_cast<uint8_t>('e'));
}

TEST_CASE("VIA register reads combine outputs and inputs using DDR", "[via]") {
    VIA via(0x7800, 12);

    via.write(VIA_REG_DDRA, 0xF0);
    via.write(VIA_REG_ORA, 0xAA);
    REQUIRE(via.read(VIA_REG_DDRA) == 0xF0);
    REQUIRE(via.read(VIA_REG_ORA) == 0xA0);

    via.write(VIA_REG_DDRB, 0x0F);
    via.write(VIA_REG_ORB, 0xA5);
    REQUIRE(via.read(VIA_REG_DDRB) == 0x0F);
    REQUIRE(via.read(VIA_REG_ORB) == 0x05);
}

TEST_CASE("VIA unimplemented registers read as 0xFF and ignore writes", "[via]") {
    VIA via(0x7800, 12);

    via.write(0x0E, 0x12);
    REQUIRE(via.read(0x0E) == 0xFF);
}

TEST_CASE("VIA responds to addresses based on configured mask", "[via]") {
    VIA via(0x7800, 12);

    REQUIRE(via.responds(0x7800));
    REQUIRE(via.responds(0x780F));
    REQUIRE_FALSE(via.responds(0x7700));
}
