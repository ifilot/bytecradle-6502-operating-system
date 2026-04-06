import os
import re
import shutil
import subprocess
import tempfile

import pytest

from harness import BCOS_ROM, EMU, read_until, resolve_sd_image, spawn_emulator, stop_emulator


def test_hexdump_shows_hex_and_ascii_column() -> None:
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
        temp_sd = os.path.join(tempdir, "hexdump.img")
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

        payload = b"TEST-HEXDUMP!\x01OK"
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
                    "::/HDUMP.BIN",
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

            os.write(master_fd, b"more HDUMP.BIN\r")
            read_until(master_fd, ":/>", timeout_s=10.0)

            os.write(master_fd, b"hexdump 1000\r")
            output = read_until(master_fd, ":/>", timeout_s=10.0)

            assert re.search(
                r"1000:\s+54 45 53 54 2D 48 45 58 44 55 4D 50 21 01 4F 4B\s+\|\s+TEST-HEXDUMP!\.OK",
                output,
            ), output
        finally:
            stop_emulator(proc, master_fd)
