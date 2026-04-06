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
        bool warnings_as_errors {false};
        bool show_help {false};
    };

    void print_usage(const char* argv0) {
        std::cout
            << "ByteCradle Emulator\n\n"
            << "Usage:\n"
            << "  " << argv0 << " -b <tiny|mini> -r <rom_path> [options]\n\n"
            << "Options:\n"
            << "  -b, --board   Board type (tiny or mini)\n"
            << "  -r, --rom     Path to ROM file\n"
            << "  -s, --sdcard  Path to SD card image (required for mini)\n"
            << "  -c, --clock   CPU clock speed in MHz (default: 16.0)\n"
            << "  -d, --debug   Enable debug mode\n"
            << "  -w, --warnings-as-errors  Treat warning exceptions as fatal errors\n"
            << "  -h, --help    Show this help message\n";
    }

    bool parse_args(int argc, char** argv, EmulatorOptions& options) {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];

            auto require_value = [&](const std::string& flag, std::string& out) -> bool {
                if (i + 1 >= argc) {
                    std::cerr << "Error: Missing value for " << flag << ".\n";
                    return false;
                }
                out = argv[++i];
                return true;
            };

            if (arg == "-h" || arg == "--help") {
                options.show_help = true;
                return true;
            } else if (arg == "-b" || arg == "--board") {
                if (!require_value(arg, options.board_type)) {
                    return false;
                }
            } else if (arg == "-r" || arg == "--rom") {
                if (!require_value(arg, options.rom_path)) {
                    return false;
                }
            } else if (arg == "-s" || arg == "--sdcard") {
                if (!require_value(arg, options.sdcard_path)) {
                    return false;
                }
            } else if (arg == "-c" || arg == "--clock") {
                std::string raw_clock;
                if (!require_value(arg, raw_clock)) {
                    return false;
                }
                try {
                    options.clock_mhz = std::stod(raw_clock);
                } catch (...) {
                    std::cerr << "Error: Invalid clock value '" << raw_clock << "'.\n";
                    return false;
                }
                if (options.clock_mhz <= 0.0) {
                    std::cerr << "Error: Clock speed must be greater than zero.\n";
                    return false;
                }
            } else if (arg == "-d" || arg == "--debug") {
                options.debug_mode = true;
            } else if (arg == "-w" || arg == "--warnings-as-errors") {
                options.warnings_as_errors = true;
            } else {
                std::cerr << "Error: Unknown argument '" << arg << "'.\n";
                return false;
            }
        }

        if (options.rom_path.empty() && !options.show_help) {
            std::cerr << "Error: ROM path must be specified using -r / --rom.\n";
            return false;
        }

        return true;
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
    if (!parse_args(argc, argv, options)) {
        print_usage(argv[0]);
        return 1;
    }

    if (options.show_help) {
        print_usage(argv[0]);
        return 0;
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
