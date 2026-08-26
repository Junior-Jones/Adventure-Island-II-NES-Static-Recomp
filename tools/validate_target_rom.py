#!/usr/bin/env python3
"""Strict V01 identity gate for the pinned Adventure Island II ROM."""
from __future__ import annotations
import argparse, hashlib, json, zlib
from pathlib import Path


def load_expected(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def inspect_bytes(data: bytes) -> dict:
    if len(data) < 16 or data[:4] != b"NES\x1a":
        raise ValueError("not an iNES/NES 2.0 file")
    h = data[:16]
    flags6, flags7 = h[6], h[7]
    if (flags7 & 0x0C) == 0x08:
        raise ValueError("expected legacy iNES, got NES 2.0")
    trainer = bool(flags6 & 0x04)
    trainer_bytes = 512 if trainer else 0
    prg = h[4] * 16384
    chr_ = h[5] * 8192
    expected_len = 16 + trainer_bytes + prg + chr_
    if len(data) != expected_len:
        raise ValueError(f"size mismatch: header requires {expected_len}, file has {len(data)}")
    mapper = (flags6 >> 4) | (flags7 & 0xF0)
    mirroring = "four-screen" if flags6 & 8 else "vertical" if flags6 & 1 else "horizontal"
    payload = data[16 + trainer_bytes:]
    return {
        "format": "iNES",
        "file_bytes": len(data),
        "file_sha256": hashlib.sha256(data).hexdigest(),
        "payload_sha256": hashlib.sha256(payload).hexdigest(),
        "payload_sha1": hashlib.sha1(payload).hexdigest(),
        "payload_crc32": f"{zlib.crc32(payload) & 0xffffffff:08x}",
        "header_hex": h.hex(),
        "mapper": mapper,
        "trainer": trainer,
        "battery": bool(flags6 & 2),
        "mirroring_header": mirroring,
        "prg_rom_bytes": prg,
        "chr_rom_bytes": chr_,
    }


def validate(rom: Path, expected_path: Path) -> tuple[bool, list[str], dict]:
    expected = load_expected(expected_path)
    try:
        actual = inspect_bytes(rom.read_bytes())
    except (OSError, ValueError) as exc:
        return False, [str(exc)], {}
    keys = [
        "format", "file_bytes", "file_sha256", "payload_sha256", "payload_sha1",
        "payload_crc32", "header_hex", "mapper", "trainer", "battery",
        "mirroring_header", "prg_rom_bytes", "chr_rom_bytes"
    ]
    errors = [f"{k}: expected {expected[k]!r}, got {actual[k]!r}" for k in keys if actual[k] != expected[k]]
    return not errors, errors, actual


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("rom", type=Path)
    ap.add_argument("--profile", type=Path, default=Path(__file__).resolve().parents[1] / "config" / "expected-rom.json")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    ok, errors, actual = validate(args.rom, args.profile)
    if args.json:
        print(json.dumps({"result": "pass" if ok else "fail", "errors": errors, "actual": actual}, indent=2, sort_keys=True))
    elif ok:
        print("PASS: exact Adventure Island II ROM identity accepted")
    else:
        for error in errors:
            print("FAIL: " + error)
    return 0 if ok else 1

if __name__ == "__main__":
    raise SystemExit(main())
