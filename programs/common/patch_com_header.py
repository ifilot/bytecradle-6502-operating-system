#!/usr/bin/env python3
"""Patch ByteCradle .COM header fields post-link.

Header layout (8 bytes):
  0-1: load address (already emitted by linker/asm)
  2-3: payload length (bytes after header)
  4:   ABI major
  5:   ABI minor
  6-7: CRC16/XMODEM of payload bytes
"""

from __future__ import annotations

import argparse
from pathlib import Path


def crc16_xmodem(data: bytes) -> int:
    crc = 0x0000
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("path", type=Path)
    p.add_argument("--abi-major", type=int, required=True)
    p.add_argument("--abi-minor", type=int, required=True)
    args = p.parse_args()

    raw = bytearray(args.path.read_bytes())
    if len(raw) < 8:
        raise SystemExit(f"{args.path} too small to contain COM header")

    payload = bytes(raw[8:])
    payload_len = len(payload)
    if payload_len > 0xFFFF:
        raise SystemExit(f"{args.path} payload too large: {payload_len}")

    crc = crc16_xmodem(payload)

    raw[2] = payload_len & 0xFF
    raw[3] = (payload_len >> 8) & 0xFF
    raw[4] = args.abi_major & 0xFF
    raw[5] = args.abi_minor & 0xFF
    raw[6] = crc & 0xFF
    raw[7] = (crc >> 8) & 0xFF

    args.path.write_bytes(raw)


if __name__ == "__main__":
    main()
