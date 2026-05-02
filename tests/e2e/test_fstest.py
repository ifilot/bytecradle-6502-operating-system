import os
import pathlib
import shutil

import pytest

from harness import BCOS_ROM, EMU, read_until, resolve_sd_image, run_cmd, spawn_emulator, stop_emulator


def test_fstest_exercises_fs_open_read_close_from_user_program(tmp_path: pathlib.Path) -> None:
    if not BCOS_ROM.exists():
        pytest.skip("BCOS ROM was not built")

    run_cmd(["bash", "emulator/script/create_sd.sh"])
    sd_image = resolve_sd_image()
    if not sd_image.exists():
        pytest.skip(
            "SD image not found. Create one with emulator/script/create_sd.sh "
            "or set BC6502_SDCARD_IMAGE."
        )
    test_sd = tmp_path / "fstest.img"
    shutil.copyfile(sd_image, test_sd)

    proc, master_fd = spawn_emulator(
        [
            str(EMU),
            "--board",
            "mini",
            "--rom",
            str(BCOS_ROM),
            "--sdcard",
            str(test_sd),
            "--clock",
            "1000.0",
            "--sdcard-write-through",
            "--warnings-as-errors",
        ]
    )

    try:
        read_until(master_fd, ":/>", timeout_s=12.0)

        os.write(master_fd, b"cd programs\r")
        read_until(master_fd, ":/PROGRAMS/>", timeout_s=10.0)

        os.write(master_fd, b"fstest\r")
        output = read_until(master_fd, ":/PROGRAMS/>", timeout_s=12.0)

        assert "PASS chdir-root" in output
        assert "PASS getcwd-root" in output
        assert "PASS open-readme" in output
        assert "PASS read-sequential" in output
        assert "PASS open-readme-block" in output
        assert "PASS block-status" in output
        assert "PASS block-read" in output
        assert "PASS block-advance-status" in output
        assert "PASS block-advance" in output
        assert "PASS multi-open" in output
        assert "PASS open-exhausted" in output
        assert "PASS open-close-read" in output
        assert "PASS read-after-close" in output
        assert "PASS open-big" in output
        assert "PASS big-read" in output
        assert "PASS big-boundary" in output
        assert "PASS big-advance" in output
        assert "PASS big-eof" in output
        assert "PASS open-empty" in output
        assert "PASS read-empty" in output
        assert "PASS block-empty" in output
        assert "PASS open-missing" in output
        assert "PASS open-ex-read" in output
        assert "PASS open-ex-missing" in output
        assert "PASS open-ex-bad-name" in output
        assert "PASS open-ex-bad-mode" in output
        assert "PASS open-ex-null" in output
        assert "PASS getcwd-small" in output
        assert "PASS getcwd-null" in output
        assert "PASS chdir-missing" in output
        assert "PASS chdir-bad-name" in output
        assert "PASS open-bad-name" in output
        assert "PASS read-byte-bad-fd" in output
        assert "PASS read-block-bad-fd" in output
        assert "PASS read-block-null" in output
        assert "PASS stat-bad-name" in output
        assert "PASS stat-null" in output
        assert "PASS readdir-null" in output
        assert "PASS close-bad-fd" in output
        assert "PASS create-multiple" in output
        assert "PASS create-bad-name" in output
        assert "PASS create-write" in output
        assert "PASS write-first" in output
        assert "PASS write-second" in output
        assert "PASS write-flush" in output
        assert "PASS create-duplicate" in output
        assert "PASS flush-bad-fd" in output
        assert "PASS open-readonly-for-write" in output
        assert "PASS write-readonly" in output
        assert "PASS flush-readonly" in output
        assert "PASS write-block-null" in output
        assert "PASS open-ex-create" in output
        assert "PASS open-ex-create-write" in output
        assert "PASS open-ex-create-flush" in output
        assert "PASS stat-written" in output
        assert "PASS open-written" in output
        assert "PASS read-written" in output
        assert "PASS verify-written" in output
        assert "PASS stat-readme" in output
        assert "PASS stat-readme-data" in output
        assert "PASS stat-programs" in output
        assert "PASS stat-programs-dir" in output
        assert "PASS stat-missing" in output
        assert "PASS readdir-zero" in output
        assert "PASS readdir-zero-data" in output
        assert "PASS readdir-eof" in output
        assert "PASS restore-programs" in output
        assert "PASS getcwd-programs" in output
        assert "FSTEST PASS" in output
        assert "FAIL " not in output
    finally:
        stop_emulator(proc, master_fd)
