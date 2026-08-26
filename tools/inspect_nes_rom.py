#!/usr/bin/env python3
"""Inspect an iNES or NES 2.0 image without identifying any commercial title."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def nes2_size(lsb: int, msb_nibble: int, unit: int) -> int:
    if msb_nibble != 0x0F:
        return ((msb_nibble << 8) | lsb) * unit
    exponent = lsb & 0x3F
    multiplier = ((lsb >> 6) * 2) + 1
    return (1 << exponent) * multiplier


def inspect(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 16 or data[:4] != b"NES\x1a":
        raise ValueError("not an iNES/NES 2.0 file")

    h = data[:16]
    flags6, flags7 = h[6], h[7]
    nes2 = (flags7 & 0x0C) == 0x08
    trainer_size = 512 if flags6 & 0x04 else 0
    mapper = (flags6 >> 4) | (flags7 & 0xF0)
    submapper = None

    if nes2:
        mapper |= (h[8] & 0x0F) << 8
        submapper = h[8] >> 4
        prg_size = nes2_size(h[4], h[9] & 0x0F, 16_384)
        chr_size = nes2_size(h[5], h[9] >> 4, 8_192)
        timing = ["NTSC", "PAL", "multi-region", "Dendy"][h[12] & 0x03]
    else:
        prg_size = h[4] * 16_384
        chr_size = h[5] * 8_192
        timing = "PAL" if h[9] & 0x01 else "NTSC (header default)"

    payload_offset = 16 + trainer_size
    expected_size = payload_offset + prg_size + chr_size
    if len(data) != expected_size:
        raise ValueError(
            f"size mismatch: header requires {expected_size} bytes, file has {len(data)}"
        )

    console_codes = {
        0: "NES/Famicom",
        1: "VS System",
        2: "PlayChoice-10",
        3: "extended console type",
    }
    mirroring = (
        "four-screen" if flags6 & 0x08 else "vertical" if flags6 & 0x01 else "horizontal"
    )
    payload = data[payload_offset:]

    result = {
        "file": str(path.resolve()),
        "format": "NES 2.0" if nes2 else "iNES",
        "console": console_codes[flags7 & 0x03],
        "mapper": mapper,
        "submapper": submapper,
        "timing": timing,
        "mirroring": mirroring,
        "battery": bool(flags6 & 0x02),
        "trainer": bool(flags6 & 0x04),
        "prg_rom_bytes": prg_size,
        "chr_rom_bytes": chr_size,
        "file_bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "payload_sha256": hashlib.sha256(payload).hexdigest(),
    }
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path, help="path to a .nes image")
    args = parser.parse_args()
    try:
        print(json.dumps(inspect(args.rom), indent=2))
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
