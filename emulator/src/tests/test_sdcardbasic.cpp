#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "sdcardbasic.h"

namespace {

uint8_t transfer_byte(SdCardBasic& sd, uint8_t tx) {
    uint8_t rx = 0;
    for (int bit = 7; bit >= 0; --bit) {
        sd.set_mosi(((tx >> bit) & 0x01U) != 0);
        sd.set_clk(true);
        rx = static_cast<uint8_t>((rx << 1) | (sd.get_miso() ? 1U : 0U));
        sd.set_clk(false);
    }
    return rx;
}

std::string create_sd_image() {
    std::array<uint8_t, 1024> data{};
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(i & 0xFFU);
    }

    const auto path = std::filesystem::temp_directory_path() / "bc6502emu_unit_sd.img";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    out.close();

    return path.string();
}

void send_command(SdCardBasic& sd, const std::array<uint8_t, 6>& cmd) {
    for (auto byte : cmd) {
        (void)transfer_byte(sd, byte);
    }
}

std::array<uint8_t, 512> read_sector(SdCardBasic& sd, uint32_t sector) {
    send_command(sd, {
        static_cast<uint8_t>(0x40 | 17),
        static_cast<uint8_t>((sector >> 24) & 0xFF),
        static_cast<uint8_t>((sector >> 16) & 0xFF),
        static_cast<uint8_t>((sector >> 8) & 0xFF),
        static_cast<uint8_t>(sector & 0xFF),
        0x01
    });

    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0x00);
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFE);

    std::array<uint8_t, 512> data{};
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = transfer_byte(sd, 0xFF);
    }

    // Consume CRC16 bytes.
    (void)transfer_byte(sd, 0xFF);
    (void)transfer_byte(sd, 0xFF);

    return data;
}

void write_sector(SdCardBasic& sd, uint32_t sector, const std::array<uint8_t, 512>& data) {
    send_command(sd, {
        static_cast<uint8_t>(0x40 | 24),
        static_cast<uint8_t>((sector >> 24) & 0xFF),
        static_cast<uint8_t>((sector >> 16) & 0xFF),
        static_cast<uint8_t>((sector >> 8) & 0xFF),
        static_cast<uint8_t>(sector & 0xFF),
        0x01
    });
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0x00);

    (void)transfer_byte(sd, 0xFE); // start token
    for (const auto value : data) {
        (void)transfer_byte(sd, value);
    }

    // CRC bytes (ignored by implementation)
    (void)transfer_byte(sd, 0x00);
    (void)transfer_byte(sd, 0x00);

    bool saw_data_accepted = false;
    for (int i = 0; i < 8; ++i) {
        if (transfer_byte(sd, 0xFF) == 0x05) {
            saw_data_accepted = true;
            break;
        }
    }
    REQUIRE(saw_data_accepted);
}

} // namespace

TEST_CASE("SD card captures CMD0 and returns idle response", "[sdcard]") {
    const std::string image = create_sd_image();
    SdCardBasic sd(image, false);

    send_command(sd, {0x40, 0x00, 0x00, 0x00, 0x00, 0x95});

    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0x01);

    std::filesystem::remove(image);
}

TEST_CASE("SD card captures CMD8 and returns interface condition", "[sdcard]") {
    const std::string image = create_sd_image();
    SdCardBasic sd(image, false);

    send_command(sd, {0x48, 0x00, 0x00, 0x01, 0xAA, 0x87});

    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0x01);
    REQUIRE(transfer_byte(sd, 0xFF) == 0x00);
    REQUIRE(transfer_byte(sd, 0xFF) == 0x00);
    REQUIRE(transfer_byte(sd, 0xFF) == 0x01);
    REQUIRE(transfer_byte(sd, 0xFF) == 0xAA);

    std::filesystem::remove(image);
}

TEST_CASE("SD card CMD17 returns data token and block bytes", "[sdcard]") {
    const std::string image = create_sd_image();
    SdCardBasic sd(image, false);

    // CMD17, block address 0, CRC byte expected to be 0x01 by implementation
    send_command(sd, {0x51, 0x00, 0x00, 0x00, 0x00, 0x01});

    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0x00);
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFE);

    REQUIRE(transfer_byte(sd, 0xFF) == 0x00);
    REQUIRE(transfer_byte(sd, 0xFF) == 0x01);
    REQUIRE(transfer_byte(sd, 0xFF) == 0x02);
    REQUIRE(transfer_byte(sd, 0xFF) == 0x03);

    std::filesystem::remove(image);
}

TEST_CASE("SD card CMD24 stores writes in memory when read-only", "[sdcard]") {
    const std::string image = create_sd_image();
    SdCardBasic sd(image, false, true);

    // CMD24, block address 1 (offset 512 bytes)
    send_command(sd, {0x58, 0x00, 0x00, 0x00, 0x01, 0x01});

    // Command response
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0x00);

    // Start token
    (void)transfer_byte(sd, 0xFE);

    for (int i = 0; i < 512; ++i) {
        (void)transfer_byte(sd, static_cast<uint8_t>(0xA0 + (i & 0x0F)));
    }

    // CRC bytes (ignored by implementation)
    (void)transfer_byte(sd, 0x12);
    (void)transfer_byte(sd, 0x34);

    // Data accepted response token (allow 1-2 byte latency through shift pipeline)
    bool saw_data_accepted = false;
    for (int i = 0; i < 8; ++i) {
        if (transfer_byte(sd, 0xFF) == 0x05) {
            saw_data_accepted = true;
            break;
        }
    }
    REQUIRE(saw_data_accepted);

    // Read block 1 back via CMD17 and confirm overlay data is returned.
    send_command(sd, {0x51, 0x00, 0x00, 0x00, 0x01, 0x01});
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0x00);
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFE);
    for (int i = 0; i < 16; ++i) {
        REQUIRE(transfer_byte(sd, 0xFF) == static_cast<uint8_t>(0xA0 + (i & 0x0F)));
    }

    // Backing image should remain unchanged in read-only mode.
    std::ifstream in(image, std::ios::binary);
    REQUIRE(in.good());
    in.seekg(512, std::ios::beg);

    std::array<uint8_t, 16> verify{};
    in.read(reinterpret_cast<char*>(verify.data()), static_cast<std::streamsize>(verify.size()));
    REQUIRE(in.gcount() == static_cast<std::streamsize>(verify.size()));
    for (size_t i = 0; i < verify.size(); ++i) {
        REQUIRE(verify[i] == static_cast<uint8_t>(i & 0xFF));
    }

    std::filesystem::remove(image);
}

TEST_CASE("SD card read-only CMD24 readback uses dirty overlay but does not persist", "[sdcard][write-retention]") {
    const std::string image = create_sd_image();
    std::array<uint8_t, 512> expected{};
    for (size_t i = 0; i < expected.size(); ++i) {
        expected[i] = static_cast<uint8_t>(0x5A ^ (i & 0xFF));
    }

    {
        SdCardBasic sd(image, false, true);
        write_sector(sd, 1, expected);

        auto readback = read_sector(sd, 1);
        REQUIRE(readback == expected);
    }

    {
        std::ifstream in(image, std::ios::binary);
        REQUIRE(in.good());
        in.seekg(512, std::ios::beg);

        std::array<uint8_t, 512> file_data{};
        in.read(reinterpret_cast<char*>(file_data.data()), static_cast<std::streamsize>(file_data.size()));
        REQUIRE(in.gcount() == static_cast<std::streamsize>(file_data.size()));
        for (size_t i = 0; i < file_data.size(); ++i) {
            REQUIRE(file_data[i] == static_cast<uint8_t>(i & 0xFF));
        }
    }

    {
        SdCardBasic sd(image, false, true);
        auto readback = read_sector(sd, 1);
        for (size_t i = 0; i < readback.size(); ++i) {
            REQUIRE(readback[i] == static_cast<uint8_t>(i & 0xFF));
        }
    }

    std::filesystem::remove(image);
}

TEST_CASE("SD card CMD24 writes through to image when write-through is enabled", "[sdcard]") {
    const std::string image = create_sd_image();
    SdCardBasic sd(image, false, false);

    send_command(sd, {0x58, 0x00, 0x00, 0x00, 0x01, 0x01});
    REQUIRE(transfer_byte(sd, 0xFF) == 0xFF);
    REQUIRE(transfer_byte(sd, 0xFF) == 0x00);

    (void)transfer_byte(sd, 0xFE);
    for (int i = 0; i < 512; ++i) {
        (void)transfer_byte(sd, static_cast<uint8_t>(0xC0 + (i & 0x0F)));
    }
    (void)transfer_byte(sd, 0x00);
    (void)transfer_byte(sd, 0x00);

    bool saw_data_accepted = false;
    for (int i = 0; i < 8; ++i) {
        if (transfer_byte(sd, 0xFF) == 0x05) {
            saw_data_accepted = true;
            break;
        }
    }
    REQUIRE(saw_data_accepted);

    std::ifstream in(image, std::ios::binary);
    REQUIRE(in.good());
    in.seekg(512, std::ios::beg);

    std::array<uint8_t, 16> verify{};
    in.read(reinterpret_cast<char*>(verify.data()), static_cast<std::streamsize>(verify.size()));
    REQUIRE(in.gcount() == static_cast<std::streamsize>(verify.size()));
    for (size_t i = 0; i < verify.size(); ++i) {
        REQUIRE(verify[i] == static_cast<uint8_t>(0xC0 + (i & 0x0F)));
    }

    std::filesystem::remove(image);
}

TEST_CASE("SD card write-through CMD24 persists and survives remount", "[sdcard][write-retention]") {
    const std::string image = create_sd_image();
    std::array<uint8_t, 512> expected{};
    for (size_t i = 0; i < expected.size(); ++i) {
        expected[i] = static_cast<uint8_t>((0x33 + i * 11) & 0xFF);
    }

    {
        SdCardBasic sd(image, false, false);
        write_sector(sd, 1, expected);

        auto readback = read_sector(sd, 1);
        REQUIRE(readback == expected);
    }

    {
        std::ifstream in(image, std::ios::binary);
        REQUIRE(in.good());
        in.seekg(512, std::ios::beg);

        std::array<uint8_t, 512> file_data{};
        in.read(reinterpret_cast<char*>(file_data.data()), static_cast<std::streamsize>(file_data.size()));
        REQUIRE(in.gcount() == static_cast<std::streamsize>(file_data.size()));
        REQUIRE(file_data == expected);
    }

    {
        SdCardBasic sd(image, false, true);
        auto readback = read_sector(sd, 1);
        REQUIRE(readback == expected);
    }

    std::filesystem::remove(image);
}

TEST_CASE("SD card can read a full custom dummy sector", "[sdcard][sector]") {
    const std::string image = create_sd_image();
    SdCardBasic sd(image, false, true);

    auto sector0 = read_sector(sd, 0);
    for (size_t i = 0; i < sector0.size(); ++i) {
        REQUIRE(sector0[i] == static_cast<uint8_t>(i & 0xFF));
    }

    auto sector1 = read_sector(sd, 1);
    for (size_t i = 0; i < sector1.size(); ++i) {
        REQUIRE(sector1[i] == static_cast<uint8_t>((i + 512) & 0xFF));
    }

    std::filesystem::remove(image);
}

TEST_CASE("SD card can write and then read back a full custom dummy sector", "[sdcard][sector]") {
    const std::string image = create_sd_image();
    SdCardBasic sd(image, false, false);

    std::array<uint8_t, 512> expected{};
    for (size_t i = 0; i < expected.size(); ++i) {
        expected[i] = static_cast<uint8_t>((i * 7) & 0xFF);
    }

    write_sector(sd, 1, expected);
    auto readback = read_sector(sd, 1);
    REQUIRE(readback == expected);

    std::ifstream in(image, std::ios::binary);
    REQUIRE(in.good());
    in.seekg(512, std::ios::beg);
    std::array<uint8_t, 512> file_data{};
    in.read(reinterpret_cast<char*>(file_data.data()), static_cast<std::streamsize>(file_data.size()));
    REQUIRE(in.gcount() == static_cast<std::streamsize>(file_data.size()));
    REQUIRE(file_data == expected);

    std::filesystem::remove(image);
}
