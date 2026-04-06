import os

import pytest

from harness import BCOS_ROM, EMU, read_until, resolve_sd_image, spawn_emulator, stop_emulator


def test_can_boot_bcos_and_run_argtest_with_arguments() -> None:
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

        os.write(master_fd, b"cd programs\r")
        read_until(master_fd, ":/PROGRAMS/>", timeout_s=10.0)

        os.write(master_fd, b"argtest one two\r")
        output = read_until(master_fd, "ARGV[2]=two", timeout_s=10.0)

        assert "ARGC=3" in output
        assert "ARGV[0]=ARGTEST" in output
        assert "ARGV[1]=one" in output
        assert "ARGV[2]=two" in output
    finally:
        stop_emulator(proc, master_fd)

def test_argtest_with_four_arguments() -> None:
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

        os.write(master_fd, b"cd programs\r")
        read_until(master_fd, ":/PROGRAMS/>", timeout_s=10.0)

        os.write(master_fd, b"argtest one two three four\r")
        output = read_until(master_fd, "ARGV[4]=four", timeout_s=10.0)

        assert "ARGC=5" in output
        assert "ARGV[0]=ARGTEST" in output
        assert "ARGV[1]=one" in output
        assert "ARGV[2]=two" in output
        assert "ARGV[3]=three" in output
        assert "ARGV[4]=four" in output
    finally:
        stop_emulator(proc, master_fd)
