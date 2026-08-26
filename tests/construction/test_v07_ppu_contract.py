#!/usr/bin/env python3
import hashlib, json
from pathlib import Path
root=Path(__file__).resolve().parents[2]
c=json.loads((root/'config/v07-ppu-contract.json').read_text())
assert c['schema']==1 and c['milestone']=='V07' and c['status']=='complete'
assert c['active_starter']=='Starter NES(4).zip'
pins=json.loads((root/'config/source-pins.json').read_text())['project_tools']['v07_ppu']
assert pins['version']=='0.7.0-v07'
assert pins['ppu_c_sha256']==hashlib.sha256((root/'src/ppu/ai2_v07_ppu.c').read_bytes()).hexdigest()
assert pins['ppu_h_sha256']==hashlib.sha256((root/'src/ppu/ai2_v07_ppu.h').read_bytes()).hexdigest()
assert c['target']['region']=='NTSC' and c['target']['mapper']==4 and c['target']['board']=='NES-TLROM-03' and c['target']['mapper_chip']=='Sharp MMC3B'
assert c['timing']['dots_per_scanline']==341 and c['timing']['scanlines_per_frame']==262
assert c['timing']['visible_width']==256 and c['timing']['visible_height']==240
assert c['timing']['odd_frame_dot_skip_when_rendering'] is True
assert c['ppu']['indexed_frame_bits']=='bits0-5 palette index; bits6-8 emphasis'
assert c['ppu']['sprite_sizes']==['8x8','8x16']
assert c['static_demand']['direct_ppu_contexts']==56
assert c['static_demand']['direct_v08_io_contexts']==17
assert c['construction_policy']['gameplay_used_for_static_construction'] is False
assert c['construction_policy']['screenshots_used_as_coverage_proof'] is False
assert c['construction_policy']['runtime_opcode_fetch'] is False
assert c['construction_policy']['runtime_opcode_decode'] is False
assert c['construction_policy']['interpreter_or_emulator_fallback'] is False
f=c['static_reset_frontier']
assert f['mode']=='v07-static-reset-frontier-no-input' and f['successful_generated_steps']==22974
assert f['current_identity']=='15:E15D' and f['first_unconstructed_machine_operation']=='15:E15D STA $4015'
assert f['cpu_cycles_at_trap']==80701 and f['ppu_cycles_at_trap']==242103 and f['apu_cycles_at_trap']==80701
assert f['completed_frame_generations']==2 and f['ppu_error']==0 and f['mapper_a12_ambiguous_while_irq_disabled']==7
assert f['trap']=='APU_IO_NOT_CONSTRUCTED'
cl=c['closure_claims']
assert cl['cold_boot'].startswith('NOT CLOSED')
assert 'ROM-wide fixed point not claimed' in cl['cpu_graph']
assert cl['machine'].startswith('PARTIAL')
assert cl['qualification']=='NOT AUTHORIZED before V10'
assert len(c['accuracy_limits'])>=4
for rel,expected in c['source_hashes'].items():
    got=hashlib.sha256((root/rel).read_bytes()).hexdigest()
    assert got==expected,(rel,expected,got)
print(f"V07 PPU contract: PASS ({len(c['source_hashes'])} source hashes; cold frontier={f['first_unconstructed_machine_operation']})")
