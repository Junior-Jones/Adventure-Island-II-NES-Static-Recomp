#!/usr/bin/env python3
import csv,json,re
from pathlib import Path
root=Path(__file__).resolve().parents[2]
contracts=json.loads((root/'generated/v05/opcode-form-contracts.json').read_text())
assert contracts['form_count']==99
# Every currently required V04 instruction form is official; no unofficial form is silently omitted.
# Opcode membership is verified against V03's pinned 2A03 metadata when available.
meta=json.loads((root/'config/2a03-opcodes.json').read_text())
assert meta['entry_count']==256==len(meta['entries'])
byop={int(r['opcode']):r for r in meta['entries']}
required=[int(f['opcode_form'],16) for f in contracts['forms']]
assert all(not byop[o]['unofficial'] for o in required)
assert sum(1 for o in required if byop[o]['unofficial'])==0

# BRK is not an admitted Adventure Island II executable context. The V06 target-specific
# CPU therefore does not add a speculative BRK execution lane; any unproved $00 identity
# is rejected by the generated bank/PC authority before semantics are selected.
with (root/'generated/v04/instructions.csv').open(newline='') as f:
    v04=list(csv.DictReader(f))
assert sum(1 for r in v04 if r['mnemonic']=='BRK')==0

texts={p.relative_to(root).as_posix():p.read_text(errors='replace') for p in [root/'src/runtime/ai2_v06_runtime.c',root/'src/runtime/ai2_v06_semantics.c']}
joined='\n'.join(texts.values()).lower()
for forbidden in ['interpret_opcode','opcode_dispatch_table','runtime_decode_opcode','fallback_interpreter','execopcode']:
    assert forbidden not in joined, forbidden
# Production semantic selection must remain generated fixed-symbol calls, not a runtime opcode switch.
assert not re.search(r'switch\s*\(\s*(?:op|opcode|byte)\b',joined,re.I)
assert 'ai2_v05_dispatch' in texts['src/runtime/ai2_v06_runtime.c']
assert 'prg_rom[' not in texts['src/runtime/ai2_v06_semantics.c']
assert all(not f['runtime_opcode_fetch'] and not f['runtime_opcode_decode'] for f in contracts['forms'])
print('V06 static-source audit: PASS (99 fixed official forms; required unofficial forms=0; admitted BRK contexts=0; runtime opcode fetch/decode absent)')
