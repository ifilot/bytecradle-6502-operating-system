import pathlib

import pytest

from harness import BCOS_ROM, EMU, read_until, spawn_emulator, stop_emulator, write_test_rom


def test_can_run_program_and_capture_output(tmp_path: pathlib.Path) -> None:
    rom_path = tmp_path / "e2e_test.bin"
    write_test_rom(rom_path)

    proc, master_fd = spawn_emulator(
        [str(EMU), "--board", "tiny", "--rom", str(rom_path), "--clock", "1.0"]
    )

    try:
        output = read_until(master_fd, "E2E OK", timeout_s=5.0)
        assert "E2E OK" in output
    finally:
        stop_emulator(proc, master_fd)


def test_bcos_binary_boots_and_prints_startup(tmp_path: pathlib.Path) -> None:
    if not BCOS_ROM.exists():
        pytest.skip("BCOS ROM was not built")

    fake_sd = tmp_path / "dummy_sd.img"
    fake_sd.write_bytes(b"\x00" * (4 * 1024 * 1024))

    proc, master_fd = spawn_emulator(
        [
            str(EMU),
            "--board",
            "mini",
            "--rom",
            str(BCOS_ROM),
            "--sdcard",
            str(fake_sd),
            "--clock",
            "1.0",
        ]
    )

    try:
        output = read_until(master_fd, "Starting system.", timeout_s=8.0)
        assert "Starting system." in output
    finally:
        stop_emulator(proc, master_fd)
