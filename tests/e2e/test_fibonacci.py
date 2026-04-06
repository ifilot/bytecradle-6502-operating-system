import os
import re

import pytest

from harness import BCOS_ROM, EMU, read_until, resolve_sd_image, spawn_emulator, stop_emulator


def test_can_boot_bcos_and_run_fibo_from_sdcard() -> None:
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
        ]
    )

    try:
        read_until(master_fd, ":/>", timeout_s=20.0)

        os.write(master_fd, b"cd programs\r")
        read_until(master_fd, ":/PROGRAMS/>", timeout_s=10.0)

        os.write(master_fd, b"fibo\r")
        output = read_until(master_fd, "10946", timeout_s=10.0)

        observed = [int(value) for value in re.findall(r"\b\d+\b", output)]
        expected_prefix = [1, 2, 3, 5, 8, 13, 21, 34, 55, 89]
        assert observed[: len(expected_prefix)] == expected_prefix
    finally:
        stop_emulator(proc, master_fd)
