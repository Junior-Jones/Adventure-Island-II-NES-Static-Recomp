#!/usr/bin/env python3
import csv,json
from pathlib import Path
root=Path(__file__).resolve().parents[2]
proof=json.loads((root/'generated/v07/ppu-static-demand.json').read_text())
rows=[]
for r in csv.DictReader((root/'generated/v04/instructions.csv').open()):
    b=bytes.fromhex(r['bytes'])
    if len(b)>=3 and r['mode'].lower().startswith('abs'):
        a=b[1]|b[2]<<8
        if 0x2000<=a<=0x2007 or 0x4000<=a<=0x4017:
            rows.append((int(r['physical_bank']),r['cpu_pc'],r['mnemonic'],r['mode'],a,r['bytes']))
ppu=[x for x in rows if 0x2000<=x[4]<=0x2007]
v08=[x for x in rows if 0x4000<=x[4]<=0x4017]
counts={f'0x{a:04X}':sum(x[4]==a for x in ppu) for a in sorted(set(x[4] for x in ppu))}
assert proof['construction_basis'].endswith('no gameplay traces')
assert proof['ppu_direct_access_contexts']==len(ppu)==56
assert proof['ppu_direct_register_counts']==counts
assert set(counts)=={'0x2000','0x2001','0x2002','0x2003','0x2005','0x2006','0x2007'}
assert proof['v08_direct_io_contexts']==len(v08)==17
assert set(proof['v08_direct_io_registers'])=={'0x4000','0x4001','0x4002','0x4003','0x4014','0x4015','0x4016','0x4017'}
print(f'V07 static PPU demand audit: PASS (PPU direct contexts={len(ppu)}; V08 direct I/O contexts={len(v08)}; gameplay traces=0)')
