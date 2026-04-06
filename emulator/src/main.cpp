/**************************************************************************
 *                                                                        *
 *   Author: Ivo Filot <ivo@ivofilot.nl>                                  *
 *                                                                        *
 *   ByteCradle 6502 EMULATOR is free software:                           *
 *   you can redistribute it and/or modify it under the terms of the      *
 *   GNU General Public License as published by the Free Software         *
 *   Foundation, either version 3 of the License, or (at your option)     *
 *   any later version.                                                   *
 *                                                                        *
 *   This program is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty          *
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.              *
 *   See the GNU General Public License for more details.                 *
 *                                                                        *
 *   You should have received a copy of the GNU General Public License    *
 *   along with this program.  If not, see http://www.gnu.org/licenses/.  *
 *                                                                        *
 **************************************************************************/

#include <thread>
#include <chrono>
#include <memory>
#include <iostream>
#include <atomic>
#include <csignal>
#include <string>

#include <CLI/CLI.hpp>

#include "bytecradletiny.h"
#include "bytecradlemini.h"
#include "terminal.h"

// anonymous namespace
namespace {
    std::atomic<bool> g_should_exit { false };

    struct EmulatorOptions {
        std::string board_type {"tiny"};
        std::string rom_path;
        std::string sdcard_path;
        double clock_mhz {16.0};
        bool debug_mode {false};
        bool sdcard_read_only {true};
        bool sdcard_write_through {false};
        bool warnings_as_errors {false};
    };

    int parse_args(int argc, char** argv, EmulatorOptions& options) {
        CLI::App app{"ByteCradle Emulator"};

        app.add_option("-b,--board", options.board_type, "Board type (tiny or mini)")
            ->check(CLI::IsMember({"tiny", "mini"}))
            ->default_val("tiny");
        app.add_option("-r,--rom", options.rom_path, "Path to ROM file")->required();
        app.add_option("-s,--sdcard", options.sdcard_path, "Path to SD card image (required for mini)");
        app.add_option("-c,--clock", options.clock_mhz, "CPU clock speed in MHz")->default_val(16.0);
        app.add_flag("-d,--debug", options.debug_mode, "Enable debug mode");
        app.add_flag("--sdcard-write-through", options.sdcard_write_through,
                     "Persist SD card writes to image file");
        app.add_flag("-w,--warnings-as-errors", options.warnings_as_errors,
                     "Treat warning exceptions as fatal errors");

        try {
            app.parse(argc, argv);
        } catch (const CLI::ParseError &e) {
            return app.exit(e);
        }

        if (options.clock_mhz <= 0.0) {
            std::cerr << "Error: Clock speed must be greater than zero.\n";
            return 1;
        }

        options.sdcard_read_only = !options.sdcard_write_through;

        return 0;
    }

    void handle_sigint(int) {
        g_should_exit = true;
    }

    bool is_exit_key(uint8_t key) {
        constexpr uint8_t ctrl_d = 0x04;
        return key == ctrl_d;
    }
}

int main(int argc, char** argv) {
    EmulatorOptions options;
    const int parse_result = parse_args(argc, argv, options);
    if (parse_result != 0) {
        return parse_result;
    }

    double cpuFrequency = options.clock_mhz * 1'000'000.0; // Convert MHz to Hz

    std::unique_ptr<ByteCradleBoard> board;
    if (options.board_type == "tiny") {
        board = std::make_unique<ByteCradleTiny>(
            options.rom_path,
            options.debug_mode,
            options.warnings_as_errors
        );
    } else if (options.board_type == "mini") {
        if (options.sdcard_path.empty()) {
            std::cerr << "Error: SD card image must be specified for 'mini' board.\n";
            return 1;
        }
        board = std::make_unique<ByteCradleMini>(
            options.rom_path,
            options.sdcard_path,
            options.sdcard_read_only,
            options.debug_mode,
            options.warnings_as_errors
        );
    } else {
        std::cerr << "Error: Invalid board type specified ('" << options.board_type
                  << "'). Must be 'tiny' or 'mini'.\n";
        return 1;
    }

    if (!TerminalRawMode::has_minimum_terminal_size(80, 25)) {
        std::cerr << "Error: Terminal size too small. Need at least 80x25.\n";
        return 1;
    }

    std::signal(SIGINT, handle_sigint);

    TerminalRawMode terminal;
    board->reset();

    constexpr auto keyboardPollInterval = std::chrono::milliseconds(20); // 20 ms keyboard poll
    auto tickInterval = std::chrono::nanoseconds(static_cast<int>(1'000'000'000.0 / cpuFrequency));

    auto lastTickTime = std::chrono::steady_clock::now();
    auto lastKeyPollTime = lastTickTime;

    while (!g_should_exit.load()) {
        auto now = std::chrono::steady_clock::now();

        // CPU ticking at precise frequency
        if (now - lastTickTime >= tickInterval) {
            board->tick();
            if (board->should_stop()) {
                g_should_exit = true;
                break;
            }
            lastTickTime += tickInterval;
        }

        // Poll keyboard every 20ms independently
        if (now - lastKeyPollTime >= keyboardPollInterval) {
            auto poll_result = TerminalRawMode::poll_key();
            if (poll_result.eof) {
                g_should_exit = true;
                break;
            }

            if (poll_result.key.has_value()) {
                if (is_exit_key(*poll_result.key)) {
                    g_should_exit = true;
                    break;
                }

                board->keypress(static_cast<char>(*poll_result.key));
            }
            lastKeyPollTime += keyboardPollInterval;
        }
    }

    std::cout << "\n[emulator] Exit requested. Shutting down...\n";

    return 0;
}
