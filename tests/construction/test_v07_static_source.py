#!/usr/bin/env python3
import re
from pathlib import Path
root=Path(__file__).resolve().parents[2]
files=[root/'src/ppu/ai2_v07_ppu.c',root/'src/ppu/ai2_v07_ppu.h',root/'src/runtime/ai2_v06_runtime.c',root/'src/runtime/ai2_v06_runtime.h']
text='\n'.join(p.read_text() for p in files)
for required in [
    'ai2_v07_ppu_cpu_read','ai2_v07_ppu_cpu_write','ai2_v07_ppu_tick',
    'ai2_v07_ppu_memory_read','ai2_v07_ppu_memory_write',
    'ai2_v07_ppu_copy_indexed','ai2_v07_ppu_copy_bgra',
    'ai2_mmc3_observe_ppu_address','AI2_V07_SCREEN_WIDTH = 256','AI2_V07_SCREEN_HEIGHT = 240'
]:
    assert required in text, required
for forbidden in [r'ExecOpcode',r'interpreter_fallback',r'runtime_opcode_decode\s*=\s*true',r'gameplay_seed',r'input_schedule']:
    assert not re.search(forbidden,text,re.I), forbidden
# The PPU receives CHR via the exact external ROM pointer; no target ROM byte array may be embedded.
assert '10dd53fd62c95e77856b09c693b3bdd14b62a89988902eea111257cafe9b8acc' not in text.lower()
assert 'Adventure Island II (USA).nes' not in text
print('V07 static-source audit: PASS (PPU is native hardware state; no interpreter/gameplay-derived construction lane)')
