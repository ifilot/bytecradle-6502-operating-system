#!/usr/bin/env python3
"""Inspect BCOS ROM usage by bank from linker map and binary.

Reports occupancy for:
- banks 0+1 combined (16 KiB window at $C000-$FFFF)
- bank 2 (8 KiB window at $A000-$BFFF)
- bank 3 (8 KiB window at $A000-$BFFF)
- bank 4 (8 KiB window at $A000-$BFFF)
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

BANK01_CAPACITY = 0x4000
BANK_CAPACITY = 0x2000

BANK01_SEGMENTS = ("STARTUP", "ONCE", "CODE", "RODATA", "JUMPTABLE", "VECTORS")

SEGMENT_RE = re.compile(r"^(\w+)\s+[0-9A-F]{6}\s+[0-9A-F]{6}\s+([0-9A-F]{6})\s+[0-9A-F]{5}$")
DATA_SIZE_RE = re.compile(r"__DATA_SIZE__\s+([0-9A-F]{6})\s+REA")


def parse_map(path: Path) -> tuple[dict[str, int], int]:
    segments: dict[str, int] = {}
    data_size = 0

    lines = path.read_text().splitlines()
    in_segment_list = False

    for line in lines:
        if line.strip() == "Segment list:":
            in_segment_list = True
            continue

        if in_segment_list:
            m = SEGMENT_RE.match(line.strip())
            if m:
                segments[m.group(1)] = int(m.group(2), 16)
            elif line.startswith("Exports list by name:"):
                in_segment_list = False

        dm = DATA_SIZE_RE.search(line)
        if dm:
            data_size = int(dm.group(1), 16)

    return segments, data_size


def percent(used: int, total: int) -> float:
    return (used / total) * 100.0 if total else 0.0


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--map", dest="map_path", required=True, type=Path)
    p.add_argument("--bin", dest="bin_path", required=True, type=Path)
    p.add_argument("--out", dest="out_path", required=True, type=Path)
    args = p.parse_args()

    segments, data_size = parse_map(args.map_path)
    bin_size = args.bin_path.stat().st_size

    bank01_used = data_size + sum(segments.get(name, 0) for name in BANK01_SEGMENTS)
    bank2_used = segments.get("B2CODE", 0) + segments.get("B2RODATA", 0)
    bank3_used = segments.get("B3CODE", 0) + segments.get("B3RODATA", 0)
    bank4_used = segments.get("B4CODE", 0) + segments.get("B4RODATA", 0)

    bank01_free = BANK01_CAPACITY - bank01_used
    bank2_free = BANK_CAPACITY - bank2_used
    bank3_free = BANK_CAPACITY - bank3_used
    bank4_free = BANK_CAPACITY - bank4_used

    report = "\n".join(
        [
            "# BCOS ROM usage report",
            "",
            f"Binary: `{args.bin_path}` ({bin_size} bytes)",
            f"Map: `{args.map_path}`",
            "",
            "## Capacity",
            "",
            f"- Banks 0+1 combined: {BANK01_CAPACITY} bytes",
            f"- Bank 2: {BANK_CAPACITY} bytes",
            f"- Bank 3: {BANK_CAPACITY} bytes",
            f"- Bank 4: {BANK_CAPACITY} bytes",
            "",
            "## Usage",
            "",
            f"- Banks 0+1 used: {bank01_used} bytes ({percent(bank01_used, BANK01_CAPACITY):.2f}%)",
            f"- Banks 0+1 free: {bank01_free} bytes",
            f"- Bank 2 used: {bank2_used} bytes ({percent(bank2_used, BANK_CAPACITY):.2f}%)",
            f"- Bank 2 free: {bank2_free} bytes",
            f"- Bank 3 used: {bank3_used} bytes ({percent(bank3_used, BANK_CAPACITY):.2f}%)",
            f"- Bank 3 free: {bank3_free} bytes",
            f"- Bank 4 used: {bank4_used} bytes ({percent(bank4_used, BANK_CAPACITY):.2f}%)",
            f"- Bank 4 free: {bank4_free} bytes",
            "",
            "## Breakdown",
            "",
            f"- DATA (load image in ROM): {data_size} bytes",
            *[f"- {name}: {segments.get(name, 0)} bytes" for name in BANK01_SEGMENTS],
            f"- B2CODE: {segments.get('B2CODE', 0)} bytes",
            f"- B2RODATA: {segments.get('B2RODATA', 0)} bytes",
            f"- B3CODE: {segments.get('B3CODE', 0)} bytes",
            f"- B3RODATA: {segments.get('B3RODATA', 0)} bytes",
            f"- B4CODE: {segments.get('B4CODE', 0)} bytes",
            f"- B4RODATA: {segments.get('B4RODATA', 0)} bytes",
            "",
        ]
    )

    print(report)
    args.out_path.parent.mkdir(parents=True, exist_ok=True)
    args.out_path.write_text(report)


if __name__ == "__main__":
    main()
