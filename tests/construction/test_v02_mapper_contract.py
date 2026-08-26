#!/usr/bin/env python3
from __future__ import annotations
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
contract = json.loads((ROOT / "config" / "mapper-contract.json").read_text(encoding="utf-8"))

assert contract["schema"] == 1
assert contract["target"]["pcb"] == "NES-TLROM-03"
assert contract["target"]["mapper"] == 4
assert contract["target"]["hardware"] == "Sharp MMC3B"
assert contract["geometry"]["prg_bank_count_8k"] == 16
assert contract["geometry"]["chr_bank_count_1k"] == 128
assert contract["ram"]["prg_ram_bytes"] == 0
assert contract["ram"]["chr_ram_bytes"] == 0
assert contract["bus_conflicts"] is False
assert contract["expansion_audio"] == "none"
assert len(contract["registers"]) == 8
assert {x["canonical"] for x in contract["registers"]} == {
    "$8000", "$8001", "$A000", "$A001", "$C000", "$C001", "$E000", "$E001"
}
assert contract["power_on"]["fixed_prg_e000_ffff_bank"] == 15
assert contract["power_on"]["switchable_state"] == "unspecified; represented explicitly as unknown"
assert contract["reset"]["mapper_state"] == "preserved"
assert contract["irq"]["revision"] == "Sharp/new behavior"
assert contract["irq"]["a12_low_filter_m2_falling_edges"] == 3
assert contract["mirroring"]["a000_bit0_0"] == "vertical mirroring; CIRAM A10 <- PPU A10"
assert contract["mirroring"]["a000_bit0_1"] == "horizontal mirroring; CIRAM A10 <- PPU A11"
assert contract["submapper"]["status"] == "legacy iNES has no NES 2.0 submapper; exact physical board evidence selects standard TLROM/Sharp MMC3B behavior"
assert contract["unresolved_hardware_blockers"] == []
print("PASS: V02 mapper-contract.json required fields and exact-board assertions")
