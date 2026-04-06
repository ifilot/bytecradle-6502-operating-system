import os
import shutil
import struct
import tempfile

import pytest

from harness import BCOS_ROM, EMU, read_until, resolve_sd_image, spawn_emulator, stop_emulator


def _read_partition_info(img: bytes) -> dict[str, int]:
    part_lba = struct.unpack_from("<I", img, 0x1C6)[0]
    bpb_off = part_lba * 512
    sectors_per_cluster = img[bpb_off + 0x0D]
    reserved_sectors = struct.unpack_from("<H", img, bpb_off + 0x0E)[0]
    number_of_fats = img[bpb_off + 0x10]
    sectors_per_fat = struct.unpack_from("<I", img, bpb_off + 0x24)[0]
    root_cluster = struct.unpack_from("<I", img, bpb_off + 0x2C)[0]
    fat1_lba = part_lba + reserved_sectors
    fat2_lba = fat1_lba + sectors_per_fat
    data_lba = fat1_lba + number_of_fats * sectors_per_fat

    return {
        "part_lba": part_lba,
        "sectors_per_cluster": sectors_per_cluster,
        "fat1_lba": fat1_lba,
        "fat2_lba": fat2_lba,
        "data_lba": data_lba,
        "root_cluster": root_cluster,
    }


def _cluster_lba(info: dict[str, int], cluster: int) -> int:
    return info["data_lba"] + (cluster - 2) * info["sectors_per_cluster"]


def _fat_entry(img: bytes, fat_lba: int, cluster: int) -> int:
    fat_off = fat_lba * 512 + cluster * 4
    return struct.unpack_from("<I", img, fat_off)[0] & 0x0FFFFFFF


def _iter_dir_entries(img: bytes, info: dict[str, int], start_cluster: int):
    cluster = start_cluster
    visited: set[int] = set()

    while 2 <= cluster < 0x0FFFFFF8 and cluster not in visited:
        visited.add(cluster)
        base = _cluster_lba(info, cluster) * 512
        span = info["sectors_per_cluster"] * 512

        for off in range(base, base + span, 32):
            first = img[off]
            if first == 0x00:
                return
            if first == 0xE5:
                continue
            attrib = img[off + 0x0B]
            if (attrib & 0x0F) == 0x0F:
                continue

            name = img[off : off + 11]
            cl_hi = struct.unpack_from("<H", img, off + 0x14)[0]
            cl_lo = struct.unpack_from("<H", img, off + 0x1A)[0]
            file_cluster = (cl_hi << 16) | cl_lo
            size = struct.unpack_from("<I", img, off + 0x1C)[0]
            yield {
                "name": name,
                "attrib": attrib,
                "cluster": file_cluster,
                "size": size,
            }

        cluster = _fat_entry(img, info["fat1_lba"], cluster)


def _find_entry(img: bytes, info: dict[str, int], dirname: bytes):
    for ent in _iter_dir_entries(img, info, info["root_cluster"]):
        if ent["name"] == dirname:
            return ent
    return None


def test_mkdir_and_touch_write_through_and_mirror_fat() -> None:
    if not BCOS_ROM.exists():
        pytest.skip("BCOS ROM was not built")

    sd_image = resolve_sd_image()
    if not sd_image.exists():
        pytest.skip(
            "SD image not found. Create one with emulator/script/create_sd.sh "
            "or set BC6502_SDCARD_IMAGE."
        )

    with tempfile.TemporaryDirectory() as tempdir:
        temp_sd = os.path.join(tempdir, "mkdir-touch.img")
        shutil.copyfile(sd_image, temp_sd)

        proc, master_fd = spawn_emulator(
            [
                str(EMU),
                "--board",
                "mini",
                "--rom",
                str(BCOS_ROM),
                "--sdcard",
                temp_sd,
                "--sdcard-write-through",
                "--clock",
                "12.0",
                "--warnings-as-errors",
            ]
        )

        try:
            read_until(master_fd, ":/>", timeout_s=20.0)

            os.write(master_fd, b"mkdir made\r")
            read_until(master_fd, ":/>", timeout_s=10.0)

            os.write(master_fd, b"cd made\r")
            read_until(master_fd, ":/MADE/>", timeout_s=10.0)

            os.write(master_fd, b"ls\r")
            made_listing = read_until(master_fd, ":/MADE/>", timeout_s=10.0)
            assert ".            " in made_listing, made_listing
            assert "..           " in made_listing, made_listing

            os.write(master_fd, b"cd ..\r")
            read_until(master_fd, ":/>", timeout_s=10.0)

            os.write(master_fd, b"touch empty.txt\r")
            read_until(master_fd, ":/>", timeout_s=10.0)
        finally:
            stop_emulator(proc, master_fd)

        img = open(temp_sd, "rb").read()
        info = _read_partition_info(img)

        made_entry = _find_entry(img, info, b"MADE       ")
        assert made_entry is not None, "Expected directory MADE in root"
        assert made_entry["attrib"] & 0x10

        touch_entry = _find_entry(img, info, b"EMPTY   TXT")
        assert touch_entry is not None, "Expected EMPTY.TXT in root"
        assert (touch_entry["attrib"] & 0x10) == 0
        assert touch_entry["size"] == 0

        made_cluster = made_entry["cluster"]
        assert made_cluster >= 2

        fat1_value = _fat_entry(img, info["fat1_lba"], made_cluster)
        fat2_value = _fat_entry(img, info["fat2_lba"], made_cluster)
        assert fat1_value == fat2_value
        assert fat1_value >= 0x0FFFFFF8
