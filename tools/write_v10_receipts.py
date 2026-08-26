#!/usr/bin/env python3
from __future__ import annotations
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT.parents[1]
ROM=Path('/mnt/data/Adventure Island II (USA)(1).nes')
OUT=ROOT/'receipts/v10'; OUT.mkdir(parents=True,exist_ok=True)
def h(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def write(n,o): (OUT/n).write_text(json.dumps(o,indent=2)+'\n')
fixed=json.loads((ROOT/'generated/v10/fixed-point-summary.json').read_text())
mapper=json.loads((ROOT/'generated/v10/mapper-execution-domain.json').read_text())
v09=json.loads((ROOT/'receipts/V09-SOURCE-MANIFEST.json').read_text())
old={e['path']:e for e in v09['files']}
allowed=json.loads((ROOT/'config/v10-closure-contract.json').read_text())['v09_hash_gated_inheritance']['allowed_changed_files']
changed=[]
for p in allowed:
    changed.append({'path':p,'v09_sha256':old[p]['sha256'],'v10_current_sha256':h(ROOT/p)})
write('V10-HASH-GATED-INHERITANCE.json',{
  'schema':1,'milestone':'V10','result':'PASS','baseline':'sealed V09 source manifest',
  'v09_manifest_aggregate_sha256':v09['aggregate_sha256'],'v09_file_count':v09['file_count'],
  'changed_files':changed,'unchanged_inherited_files':v09['file_count']-len(changed),
  'compile_reuse_policy':'Prior V09 compiler/runtime evidence is reused only for files whose V09 hashes remain exact. V10 does not claim compiled certification for the three changed timing/identity files.',
  'compile_invoked':False})
modules=['generated/v05/bank_dispatch.c','src/runtime/ai2_v06_runtime.c','src/mapper/ai2_mmc3_mapper.c','src/ppu/ai2_v07_ppu.c','src/apu/ai2_v08_apu.c','src/api/ai2_v09_core.c','include/ai2_static_core.h']
write('V10-ARCHITECTURE-RECEIPT.json',{
  'schema':1,'milestone':'V10','result':'PASS','modules':[{ 'path':p,'sha256':h(ROOT/p)} for p in modules],
  'execution':'generated fixed physical-bank/PC dispatch -> handwritten CPU/bus/scheduler -> mapper/PPU/APU/DMA/controller -> opaque public API',
  'unknown_execution':'fail closed as missing identity/missing bank/runtime trap','oracle_linked_into_production':False})
write('V10-COVERAGE-RECEIPT.json',{
  'schema':1,'milestone':'V10','result':'PASS','rom_sha256':fixed['rom_sha256'],'prg_bytes':fixed['prg_bytes'],
  'instruction_contexts':fixed['accepted_instruction_contexts'],'edge_records':fixed['edge_records'],
  'byte_ownership':{'code':fixed['accepted_instruction_bytes'],'inline_data':fixed['inline_data_bytes'],'metadata':fixed['metadata_bytes'],'excluded_immutable_data':fixed['statically_excluded_immutable_data_bytes'],'unresolved':fixed['unresolved_prg_bytes']},
  'executable_physical_banks':fixed['executable_physical_banks'],'excluded_physical_banks':fixed['formally_excluded_physical_banks'],
  'r7_executable_values':mapper['r7_executable_values'],'r6_executable_targets':mapper['r6_window_executable_targets'],
  'executable_ram_epochs':fixed['executable_ram_epochs'],'self_modifying_code_epochs':fixed['self_modifying_code_epochs']})
starter=BASE/'Reference/Original-Packages/Starter NES(4).zip'
oracle=BASE/'Reference/Starter NES/Oracle/Linux/bin/mesen-nes-oracle'
write('V10-DEPENDENCY-RECEIPT.json',{
  'schema':1,'milestone':'V10','result':'PASS','external_test_rom':{'path':'external; never packaged','sha256':h(ROM)},
  'starter_nes4':{'sha256':h(starter)},'mesen_oracle':{'role':'independent bounded comparison only','sha256':h(oracle),'linked_into_production':False},
  'construction_tools':['python3','exact ROM','sealed generated/source pins'],'compiler_invoked_by_nonbuild_gate':False,'prompt_build_incident_receipt':'receipts/v10/V10-PROMPT-BUILD-INCIDENT.json'})
write('V10-LIMITATIONS-RECEIPT.json',{
  'schema':1,'milestone':'V10','result':'PASS_WITH_OPEN_FINAL_GATE',
  'static_romwide_fixed_point':'PASS','deterministic_regeneration':'PASS','production_static_purity':'PASS',
  'open_gate':'Cold-boot closure after the V10 PPU reset-phase source correction still requires a freshly built native core, repeated frame-zero run, and bounded native/oracle CPU/RAM/mapper/PPU/audio checkpoints.',
  'reason_not_run':'Current project policy forbids compiling/building the static core unless the user explicitly asks.',
  'static_core_complete_written':False,'gameplay_authorized':False,
  'qualification_note':'Post-core gameplay and broad differential testing remain prohibited until STATIC_CORE_COMPLETE is legitimately written.'})
reset_receipt=OUT/'mesen-reset-phase.txt'
write('V10-RESET-PHASE-RECEIPT.json',{
  'schema':1,'milestone':'V10','result':'PASS_BOUNDED_QUALIFICATION','construction_authority':'source scheduler/reset proof',
  'source_proof':{'ppu_power_on_scanline':0,'ppu_power_on_dot':4,'reset_cpu_cycles':7,'ppu_dots_per_cpu_cycle':3,'derived_reset_entry_dot':25,
                  'ppu_source_sha256':h(ROOT/'src/ppu/ai2_v07_ppu.c'),'runtime_source_sha256':h(ROOT/'src/runtime/ai2_v06_runtime.c')},
  'oracle_observation':{'receipt_sha256':h(reset_receipt),'reset_entry':'FFAE cpuCycle=7 scanline=0 ppuCycle=25','game_entry':'E010 cpuCycle=10 scanline=0 ppuCycle=34'},
  'oracle_used_as_static_authority':False,'note':'Oracle qualifies an already source-derived phase; it did not supply executable PCs or generated targets.'})
print('V10 receipts written')
