import os

import pytest

from harness import BCOS_ROM, EMU, read_until, resolve_sd_image, spawn_emulator, stop_emulator


def test_ls_and_dir_work_across_multiple_folders() -> None:
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

        os.write(master_fd, b"ls\r")
        root_listing = read_until(master_fd, ":/>", timeout_s=10.0)
        assert "PROGRAMS" in root_listing
        assert "FOLDER1" in root_listing
        assert "FOLDER2" in root_listing

        os.write(master_fd, b"cd folder1\r")
        read_until(master_fd, ":/FOLDER1/>", timeout_s=10.0)

        os.write(master_fd, b"dir\r")
        folder_listing = read_until(master_fd, ":/FOLDER1/>", timeout_s=10.0)
        assert "SUB1" in folder_listing
        assert "SUB2" in folder_listing
    finally:
        stop_emulator(proc, master_fd)
