import os
import re
import shutil
import subprocess
import tempfile

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
            "--warnings-as-errors",
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


def test_ls_shows_summary_and_pages_after_24_lines() -> None:
    if not BCOS_ROM.exists():
        pytest.skip("BCOS ROM was not built")

    if any(shutil.which(tool) is None for tool in ("parted", "mcopy")):
        pytest.skip("This test requires parted and mtools (mcopy)")

    sd_image = resolve_sd_image()
    if not sd_image.exists():
        pytest.skip(
            "SD image not found. Create one with emulator/script/create_sd.sh "
            "or set BC6502_SDCARD_IMAGE."
        )

    with tempfile.TemporaryDirectory() as tempdir:
        temp_sd = os.path.join(tempdir, "ls-paging.img")
        shutil.copyfile(sd_image, temp_sd)

        parted_output = subprocess.check_output(
            ["parted", "-ms", temp_sd, "unit", "s", "print"], text=True
        )
        part_start_sector = next(
            (
                int(line.split(":")[1].rstrip("s"))
                for line in parted_output.splitlines()
                if line.startswith("1:")
            ),
            None,
        )
        assert part_start_sector is not None, "Could not determine FAT32 partition start sector"
        offset_bytes = part_start_sector * 512
        mtools_image = f"{temp_sd}@@{offset_bytes}"

        for idx in range(1, 26):
            payload = f"file-{idx}\n".encode("ascii")
            with tempfile.NamedTemporaryFile(dir=tempdir, delete=False) as file_handle:
                file_handle.write(payload)
                file_path = file_handle.name
            try:
                subprocess.run(
                    [
                        "mcopy",
                        "-i",
                        mtools_image,
                        file_path,
                        f"::/F{idx:02d}.TXT",
                    ],
                    check=True,
                )
            finally:
                os.unlink(file_path)

        proc, master_fd = spawn_emulator(
            [
                str(EMU),
                "--board",
                "mini",
                "--rom",
                str(BCOS_ROM),
                "--sdcard",
                temp_sd,
                "--clock",
                "12.0",
                "--warnings-as-errors",
            ]
        )

        try:
            read_until(master_fd, ":/>", timeout_s=20.0)

            os.write(master_fd, b"ls\r")
            first_page = read_until(
                master_fd,
                "--- Press any key for next page, Q to quit ---",
                timeout_s=10.0,
            )
            assert "F01" in first_page

            os.write(master_fd, b"x")
            full_output = read_until(master_fd, ":/>", timeout_s=10.0)
            assert re.search(r"\d+ File\(s\) \d+ bytes", full_output), full_output
        finally:
            stop_emulator(proc, master_fd)
