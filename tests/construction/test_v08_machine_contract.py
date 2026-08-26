#!/usr/bin/env python3
import hashlib,json
from pathlib import Path
root=Path(__file__).resolve().parents[2]
d=json.loads((root/'config/v08-machine-contract.json').read_text())
assert d['milestone']=='V08' and d['status']=='complete'
assert d['construction_policy']['gameplay_used_for_static_construction'] is False
assert d['construction_policy']['runtime_opcode_fetch'] is False and d['construction_policy']['runtime_opcode_decode'] is False
assert len(d['apu']['channels'])==5 and d['target']['expansion_audio'] is False
assert d['cold_boot_endurance']['trap_code']==0 and d['cold_boot_endurance']['generated_steps']==2_000_000
assert d['cold_boot_endurance']['controller_or_gameplay_input_used'] is False
assert d['closure_claims']['cold_boot'].startswith('NOT CLOSED')
for rel,want in d['source_hashes'].items():
    got=hashlib.sha256((root/rel).read_bytes()).hexdigest(); assert got==want,(rel,got,want)
print(f'V08 machine contract: PASS ({len(d["source_hashes"])} source pins; cold-boot claim kept separate)')
