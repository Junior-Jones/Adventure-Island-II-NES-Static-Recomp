#!/usr/bin/env python3
from __future__ import annotations
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
C=json.loads((ROOT/'config/v06-runtime-contract.json').read_text())
P=json.loads((ROOT/'config/source-pins.json').read_text())
checks=0
def ck(v,msg):
    global checks
    checks+=1
    if not v: raise AssertionError(msg)
def sha(rel): return hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()
ck(C['schema']==1 and C['milestone']=='V06' and C['status']=='complete','identity')
ck(C['active_starter']=='Starter NES(4).zip','starter')
pol=C['construction_policy']
for k in ('gameplay_used_for_static_construction','runtime_opcode_fetch','runtime_opcode_decode','interpreter_or_emulator_fallback'):
    ck(pol[k] is False,k)
ck(C['cpu']['accepted_v04_instruction_contexts']==16544,'contexts')
ck(C['cpu']['fixed_opcode_forms']==99,'forms')
ck(C['cpu']['required_unofficial_forms']==0,'unofficial')
ck(C['cpu']['admitted_brk_contexts']==0,'BRK')
ck(C['cpu']['decimal_arithmetic_active'] is False,'NES decimal')
ck(C['cpu']['jmp_indirect_page_wrap'] is True,'JMP wrap')
ck(C['cpu']['rmw_order']==['read','write-old','write-new'],'RMW')
ck(C['scheduler']['cpu_ticks_per_cpu_cycle']==1,'CPU scheduler')
ck(C['scheduler']['apu_ticks_per_cpu_cycle']==1,'APU scheduler')
ck(C['scheduler']['dma_ticks_per_cpu_cycle']==1,'DMA scheduler')
ck(C['scheduler']['ppu_ticks_per_cpu_cycle']==3,'PPU scheduler')
ck(C['scheduler']['mapper_m2_falling_edges_per_cpu_cycle']==1,'mapper scheduler')
ck(C['scheduler']['frontend_owns_timing'] is False,'frontend timing')
f=C['static_reset_frontier']
ck(f['mode']=='static-reset-frontier-no-input','frontier mode')
ck(f['successful_generated_steps']==4,'frontier steps')
ck(f['first_unconstructed_machine_operation']=='15:E014 STX $2000','frontier PC')
ck(f['trap']=='PPU_NOT_CONSTRUCTED','frontier trap')
ck((f['cpu_cycles_at_trap'],f['ppu_cycles_at_trap'],f['apu_cycles_at_trap'])==(19,57,19),'frontier cycles')
ck(f['cold_boot_closure_claimed'] is False,'cold boot truth')
for rel,h in C['source_hashes'].items(): ck(sha(rel)==h,'source hash '+rel)
pin=P['project_tools']['v06_runtime']
ck(pin['version']=='0.6.0-v06','pin version')
ck(pin['runtime_c_sha256']==sha('src/runtime/ai2_v06_runtime.c'),'runtime c pin')
ck(pin['runtime_h_sha256']==sha('src/runtime/ai2_v06_runtime.h'),'runtime h pin')
ck(pin['semantics_c_sha256']==sha('src/runtime/ai2_v06_semantics.c'),'semantics pin')
ck(pin['static_frontier_tool_sha256']==sha('tools/v06_static_reset_frontier.c'),'frontier tool pin')
ck((ROOT/'VERSION.txt').read_text().strip()=='Version 06','version file')
print(f'V06 runtime contract: PASS ({checks} checks)')
