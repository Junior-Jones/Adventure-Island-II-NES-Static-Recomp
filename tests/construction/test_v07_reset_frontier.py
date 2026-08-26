#!/usr/bin/env python3
import json, subprocess, sys, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
if len(sys.argv)!=2: raise SystemExit('usage: test_v07_reset_frontier.py ROM.nes')
rom=Path(sys.argv[1])
incs=['-I'+str(root),'-I'+str(root/'src'),'-I'+str(root/'src/runtime'),'-I'+str(root/'src/mapper'),'-I'+str(root/'src/ppu'),'-I'+str(root/'generated/v05')]
sources=[root/'src/mapper/ai2_mmc3_mapper.c',root/'src/runtime/ai2_v06_runtime.c',root/'src/runtime/ai2_v06_semantics.c',root/'src/ppu/ai2_v07_ppu.c',*sorted((root/'generated/v05').glob('*.c'))]
with tempfile.TemporaryDirectory(prefix='ai2-v07-frontier-') as td:
    exe=Path(td)/'frontier'
    subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic',*incs,str(root/'tools/v07_static_reset_frontier.c'),*[str(x) for x in sources],'-o',str(exe)],check=True)
    outs=[]
    for _ in range(2):
        p=subprocess.run([str(exe),str(rom)],text=True,capture_output=True)
        if p.returncode!=0:
            raise AssertionError((p.returncode,p.stdout,p.stderr))
        outs.append(p.stdout.strip())
    assert outs[0]==outs[1], (outs[0],outs[1])
    d=json.loads(outs[0])
    assert d['mode']=='v07-static-reset-frontier-no-input'
    assert d['steps_completed']==22974, d
    assert d['current_bank']==15 and d['current_pc']==0xE15D, d
    assert d['cpu_cycles']==80701 and d['ppu_cycles']==242103 and d['apu_cycles']==80701, d
    assert d['ppu_cycles']==d['cpu_cycles']*3
    assert d['frame_generation']==2
    assert d['ppu_error']==0
    assert d['a12_ambiguous_disabled']==7
    assert d['trap_code']==6 and d['trap_source_pc']==0xE15D and d['trap_address']==0x4015 and d['trap_is_write']==1 and d['trap_value']==0x0F, d
    print(outs[0])
    print('V07 deterministic static-reset frontier: PASS (2 identical no-input runs; no gameplay)')
