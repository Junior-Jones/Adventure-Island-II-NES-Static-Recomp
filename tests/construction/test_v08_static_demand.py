#!/usr/bin/env python3
import csv,json
from pathlib import Path
root=Path(__file__).resolve().parents[2]
proof=json.loads((root/'generated/v08/apu-dma-input-static-demand.json').read_text())
rows=[]
for r in csv.DictReader((root/'generated/v04/instructions.csv').open()):
    b=bytes.fromhex(r['bytes'])
    if len(b)>=3 and r['mode'].lower().startswith('abs'):
        a=b[1]|b[2]<<8
        if 0x4000<=a<=0x4017: rows.append((int(r['physical_bank']),r['cpu_pc'],r['mnemonic'],r['mode'],a,r['bytes']))
counts={f'0x{a:04X}':sum(x[4]==a for x in rows) for a in sorted(set(x[4] for x in rows))}
assert proof['construction_basis'].endswith('no gameplay traces')
assert proof['gameplay_seeds_used']==0
assert proof['direct_or_indexed_io_contexts']==len(rows)==17
assert proof['base_register_counts']==counts
assert set(counts)=={'0x4000','0x4001','0x4002','0x4003','0x4014','0x4015','0x4016','0x4017'}
assert len(proof['indexed_audio_contexts'])==10
assert proof['channels_implemented']==['pulse1','pulse2','triangle','noise','DMC']
assert proof['expansion_audio_required'] is False
print(f'V08 static APU/DMA/input demand audit: PASS (contexts={len(rows)}; indexed audio={len(proof["indexed_audio_contexts"])}; gameplay seeds=0)')
