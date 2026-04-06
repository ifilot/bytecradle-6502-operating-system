import os
import re

import pytest

from harness import BCOS_ROM, EMU, read_until, resolve_sd_image, spawn_emulator, stop_emulator

VERSION_PATTERN = re.compile(r"Version:\s+\d+\.\d+\.\d+")
BUILD_PATTERN = re.compile(
    r"Build date:\s+[A-Z][a-z]{2}\s+\d{1,2}\s+\d{4}\s+\d{2}:\d{2}:\d{2}"
)


def test_boot_prints_version_and_build_timestamp() -> None:
    if not BCOS_ROM.exists():
        pytest.skip("BCOS ROM was not built")

    sd_image = resolve_sd_image()
    if not sd_image.exists():
        pytest.skip(
            "SD image not found. Create one with emulator/script/create_sd.sh "
            "or set BC6502_SDCARD_IMAGE."
        )

    proc, master_fd = spawn_emulator(
        [
            str(EMU),
            "--board",
            "mini",
            "--rom",
            str(BCOS_ROM),
            "--sdcard",
            str(sd_image),
            "--clock",
            "12.0",
            "--warnings-as-errors",
        ]
    )

    try:
        output = read_until(master_fd, ":/>", timeout_s=20.0)
        assert "Starting system." in output
        assert VERSION_PATTERN.search(output) is not None
        assert BUILD_PATTERN.search(output) is not None
    finally:
        stop_emulator(proc, master_fd)


def test_version_command_prints_version_and_build_timestamp() -> None:
    if not BCOS_ROM.exists():
        pytest.skip("BCOS ROM was not built")

    sd_image = resolve_sd_image()
    if not sd_image.exists():
        pytest.skip(
            "SD image not found. Create one with emulator/script/create_sd.sh "
            "or set BC6502_SDCARD_IMAGE."
        )

    proc, master_fd = spawn_emulator(
        [
            str(EMU),
            "--board",
            "mini",
            "--rom",
            str(BCOS_ROM),
            "--sdcard",
            str(sd_image),
            "--clock",
            "12.0",
            "--warnings-as-errors",
        ]
    )

    try:
        read_until(master_fd, ":/>", timeout_s=20.0)
        os.write(master_fd, b"version\r")
        output = read_until(master_fd, ":/>", timeout_s=10.0)
        assert VERSION_PATTERN.search(output) is not None
        assert BUILD_PATTERN.search(output) is not None
    finally:
        stop_emulator(proc, master_fd)
