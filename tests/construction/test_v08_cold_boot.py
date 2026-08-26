#!/usr/bin/env python3
import json,subprocess,sys,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
if len(sys.argv)!=2: raise SystemExit('usage: test_v08_cold_boot.py ROM.nes')
rom=Path(sys.argv[1])
incs=['-I'+str(root),'-I'+str(root/'src'),'-I'+str(root/'src/runtime'),'-I'+str(root/'src/mapper'),'-I'+str(root/'src/ppu'),'-I'+str(root/'src/apu'),'-I'+str(root/'generated/v05')]
sources=[root/'src/mapper/ai2_mmc3_mapper.c',root/'src/runtime/ai2_v06_runtime.c',root/'src/runtime/ai2_v06_semantics.c',root/'src/ppu/ai2_v07_ppu.c',root/'src/apu/ai2_v08_apu.c',*sorted((root/'generated/v05').glob('*.c'))]
with tempfile.TemporaryDirectory(prefix='ai2-v08-cold-') as td:
    exe=Path(td)/'cold'
    subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic',*incs,str(root/'tools/v08_static_cold_boot.c'),*[str(x) for x in sources],'-o',str(exe)],check=True)
    outs=[]
    for _ in range(2):
        p=subprocess.run([str(exe),str(rom)],text=True,capture_output=True,check=True)
        outs.append(p.stdout.strip())
    assert outs[0]==outs[1], 'V08 cold-boot runs differ'
    d=json.loads(outs[0])
    assert d['mode']=='v08-static-cold-boot-no-input'
    assert d['steps_completed']==d['max_steps']==2_000_000
    assert d['trap_code']==0 and d['ppu_error']==0
    assert d['ppu_cycles']==d['cpu_cycles']*3 and d['apu_cycles']==d['cpu_cycles']
    assert d['frame_generation']>=200
    assert d['controller_reads']>0 and d['controller_latches']>0
    assert d['oam_dma_count']>0 and d['oam_dma_cycles']>0
    assert d['apu_register_writes']>0 and d['pcm_total_samples']>0
    print(outs[0])
    print('V08 deterministic no-input cold-boot endurance: PASS (2 identical runs; no gameplay; cold-boot closure NOT claimed without oracle checkpoints)')
