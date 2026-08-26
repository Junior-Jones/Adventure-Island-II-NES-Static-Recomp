#!/usr/bin/env python3
from __future__ import annotations
import argparse, importlib.util, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("validate_target_rom", ROOT / "tools" / "validate_target_rom.py")
mod = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(mod)
PROFILE = ROOT / "config" / "expected-rom.json"

def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)

def check_reject(data: bytes, label: str) -> None:
    with tempfile.TemporaryDirectory() as td:
        p = Path(td) / f"{label}.nes"
        p.write_bytes(data)
        ok, _, _ = mod.validate(p, PROFILE)
        require(not ok, f"{label} was incorrectly accepted")

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path)
    args = ap.parse_args()
    original = args.rom.read_bytes()
    ok, errors, _ = mod.validate(args.rom, PROFILE)
    require(ok, "exact ROM rejected: " + "; ".join(errors))
    altered = bytearray(original); altered[-1] ^= 0x01
    check_reject(bytes(altered), "altered")
    malformed = bytearray(original); malformed[0:4] = b"BAD!"
    check_reject(bytes(malformed), "malformed-header")
    check_reject(original[:-1], "truncated")
    check_reject(original + b"\x00", "trailing-byte")
    print("PASS exact accepted; altered/malformed/truncated/trailing images rejected")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
