# Set minimum CMake requirements
cmake_minimum_required(VERSION 3.14)
project(bc6502emu LANGUAGES C CXX)

# Enable release build
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(EMULATOR_CORE_SOURCES
    acia.cpp
    bytecradleboard.cpp
    bytecradlemini.cpp
    bytecradletiny.cpp
    sdcardbasic.cpp
    terminal.cpp
    via.cpp
)

include(FetchContent)
set(CPP65_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CPP65_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(CLI11)

FetchContent_Declare(
    cpp65
    GIT_REPOSITORY https://github.com/ifilot/cpp65.git
    GIT_TAG v1.0.0
)
FetchContent_GetProperties(cpp65)
if(NOT cpp65_POPULATED)
    FetchContent_Populate(cpp65)
    add_subdirectory(${cpp65_SOURCE_DIR} ${cpp65_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

add_library(bc6502emu_core STATIC ${EMULATOR_CORE_SOURCES})
target_include_directories(bc6502emu_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bc6502emu_core PUBLIC cpp65_core)

add_executable(bc6502emu main.cpp)
target_link_libraries(bc6502emu PRIVATE bc6502emu_core CLI11::CLI11)

include(CTest)
if(BUILD_TESTING)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.0
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(bc6502emu_unit_tests
        tests/test_acia_via.cpp
        tests/test_processor.cpp
        tests/test_sdcardbasic.cpp
    )
    target_link_libraries(bc6502emu_unit_tests PRIVATE bc6502emu_core Catch2::Catch2WithMain)

    include(Catch)
    catch_discover_tests(bc6502emu_unit_tests)
endif()
