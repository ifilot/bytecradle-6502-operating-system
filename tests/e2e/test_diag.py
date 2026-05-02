import os

import pytest

from harness import BCOS_ROM, EMU, read_until, resolve_sd_image, spawn_emulator, stop_emulator


@pytest.mark.e2e_timeout(80)
def test_can_run_diag_program_from_sdcard() -> None:
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
            "1000.0", # basically as quick as possible
            "--warnings-as-errors",
        ]
    )

    try:
        read_until(master_fd, ":/>", timeout_s=20.0)

        os.write(master_fd, b"cd programs\r")
        read_until(master_fd, ":/PROGRAMS/>", timeout_s=10.0)

        os.write(master_fd, b"diag\r")
        output = read_until(master_fd, ":/PROGRAMS/>", timeout_s=60.0)

        assert "=== BYTECRADLE DIAGNOSTICS (DIAG) ===" in output
        assert "Testing writable banked RAM: $8000-$9FFF" in output
        assert "Bank range: $04-$3F (safe non-OS banks)" in output
        assert "SUMMARY: PASS (60/60 banks)" in output
        assert "RAM bank restored to" in output
    finally:
        stop_emulator(proc, master_fd)
