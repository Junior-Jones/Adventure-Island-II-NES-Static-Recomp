#!/usr/bin/env python3
import json, subprocess, sys, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
if len(sys.argv)!=2: raise SystemExit('usage: test_v06_reset_frontier.py ROM.nes')
rom=Path(sys.argv[1])
incs=['-I'+str(root),'-I'+str(root/'src'),'-I'+str(root/'src/mapper'),'-I'+str(root/'generated/v05')]
sources=[root/'src/mapper/ai2_mmc3_mapper.c',root/'src/runtime/ai2_v06_runtime.c',root/'src/runtime/ai2_v06_semantics.c',*sorted((root/'generated/v05').glob('*.c'))]
with tempfile.TemporaryDirectory(prefix='ai2-v06-frontier-') as td:
    exe=Path(td)/'frontier'
    subprocess.run(['gcc','-std=c11','-O0','-Wall','-Wextra','-Werror','-pedantic',*incs,str(root/'tools/v06_static_reset_frontier.c'),*[str(x) for x in sources],'-o',str(exe)],check=True)
    outs=[]
    for _ in range(2):
        p=subprocess.run([str(exe),str(rom)],check=True,text=True,capture_output=True)
        outs.append(p.stdout.strip())
    assert outs[0]==outs[1], (outs[0],outs[1])
    d=json.loads(outs[0])
    assert d['mode']=='static-reset-frontier-no-input'
    assert d['steps_completed']>0
    assert d['ppu_cycles']==d['cpu_cycles']*3
    assert d['apu_cycles']==d['cpu_cycles']
    assert d['trap_code'] in (5,6,7), d
    assert d['steps_completed']==4 and d['current_bank']==15 and d['current_pc']==0xE014, d
    assert d['trap_code']==5 and d['trap_source_pc']==0xE014 and d['trap_address']==0x2000 and d['trap_is_write']==1 and d['trap_value']==0, d
    print(outs[0])
    print('V06 deterministic static-reset frontier: PASS (2 identical no-input runs; no gameplay)')
