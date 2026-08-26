#!/usr/bin/env python3
import re
from pathlib import Path
root=Path(__file__).resolve().parents[2]
files=[root/'src/apu/ai2_v08_apu.c',root/'src/apu/ai2_v08_apu.h',root/'src/runtime/ai2_v06_runtime.c',root/'src/runtime/ai2_v06_runtime.h']
text='\n'.join(p.read_text() for p in files)
for required in ['ai2_v08_apu_io_read','ai2_v08_apu_io_write','ai2_v08_oam_dma_start','ai2_v08_dma_drain','ai2_v08_apu_tick','ai2_v08_set_controller_live','controller_read','latch(a)','ai2_v08_copy_pcm','ai2_v08_current_mix','hooks.dma_drain']:
    assert required in text,required
for forbidden in [r'ExecOpcode',r'interpreter_fallback',r'runtime_opcode_decode\s*=\s*true',r'gameplay_seed',r'input_schedule',r'frame.*keyboard',r'poll.*keyboard']:
    assert not re.search(forbidden,text,re.I),forbidden
assert '10dd53fd62c95e77856b09c693b3bdd14b62a89988902eea111257cafe9b8acc' not in text.lower()
assert 'Adventure Island II (USA).nes' not in text
print('V08 static-source audit: PASS (native APU/DMA/controller machine; no runtime opcode or gameplay-derived lane)')
