#!/usr/bin/env python3
import shutil, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
incs=['-I'+str(root),'-I'+str(root/'src'),'-I'+str(root/'src/runtime'),'-I'+str(root/'src/mapper'),'-I'+str(root/'src/ppu'),'-I'+str(root/'src/apu'),'-I'+str(root/'generated/v05')]
sources=[root/'tests/construction/test_v08_apu_dma_input.c',root/'src/apu/ai2_v08_apu.c',root/'src/ppu/ai2_v07_ppu.c',root/'src/runtime/ai2_v06_runtime.c',root/'src/mapper/ai2_mmc3_mapper.c']
compilers=[cc for cc in ('gcc','clang') if shutil.which(cc)]
assert set(compilers)=={'gcc','clang'}, f'V08 certification requires GCC and Clang; found {compilers}'
base=['-std=c11','-Wall','-Wextra','-Werror','-pedantic',*incs,*map(str,sources)]
with tempfile.TemporaryDirectory(prefix='ai2-v08-build-') as td:
    td=Path(td)
    for cc in compilers:
        exe=td/f'v08-{cc}'
        subprocess.run([cc,*base,'-O2','-o',str(exe)],check=True)
        p=subprocess.run([str(exe)],check=True,text=True,capture_output=True)
        assert 'PASS (8573 checks)' in p.stdout,p.stdout
        print(f'{cc}: {p.stdout.strip()}')
    exe=td/'v08-clang-san'
    subprocess.run(['clang',*base,'-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-o',str(exe)],check=True)
    env={'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1','PATH':'/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin'}
    p=subprocess.run([str(exe)],check=True,text=True,capture_output=True,env=env)
    assert 'PASS (8573 checks)' in p.stdout,p.stdout
    print('clang-asan-ubsan:',p.stdout.strip())
print('V08 APU/DMA/input compiler/sanitizer gate: PASS')
