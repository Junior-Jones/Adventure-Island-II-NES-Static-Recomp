#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, os, re, subprocess, sys, tempfile
from pathlib import Path

ROM_SHA='10dd53fd62c95e77856b09c693b3bdd14b62a89988902eea111257cafe9b8acc'

def h(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def tree(d):
    d=Path(d)
    return {str(p.relative_to(d)):h(p) for p in sorted(d.rglob('*')) if p.is_file()}
def run(cmd): subprocess.run(cmd,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
def check(cond,name,detail,checks):
    checks.append({'name':name,'result':'PASS' if cond else 'FAIL','detail':detail})
    if not cond: raise AssertionError(f'{name}: {detail}')

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--rom',required=True,type=Path); ap.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2]); ap.add_argument('--json-out',type=Path)
    a=ap.parse_args(); root=a.root.resolve(); rom=a.rom.resolve(); checks=[]
    contract=json.loads((root/'config/v10-closure-contract.json').read_text())
    check(h(rom)==ROM_SHA,'exact_rom',h(rom),checks)

    # V09 inherited authority: avoid repeated expensive ancestor rebuilds.
    m=json.loads((root/'receipts/V09-SOURCE-MANIFEST.json').read_text())
    check(m['aggregate_sha256']==contract['v09_hash_gated_inheritance']['source_manifest_aggregate_sha256'],'v09_manifest_pin',m['aggregate_sha256'],checks)
    allowed=set(contract['v09_hash_gated_inheritance']['allowed_changed_files']); changed=[]; missing=[]
    for e in m['files']:
        p=root/e['path']
        if not p.exists(): missing.append(e['path']); continue
        if h(p)!=e['sha256']: changed.append(e['path'])
    check(not missing,'v09_inherited_files_present',{'missing':missing},checks)
    check(set(changed)==allowed,'v09_change_allowlist',{'changed':changed,'allowed':sorted(allowed)},checks)

    with tempfile.TemporaryDirectory(prefix='ai2-v10-nonbuild-') as td:
        td=Path(td)
        # V04 clean deterministic regeneration.
        run([sys.executable,str(root/'tools/analyze_v04_control_flow.py'),str(rom),str(td/'v04a'),'--root',str(root)])
        run([sys.executable,str(root/'tools/analyze_v04_control_flow.py'),str(rom),str(td/'v04b'),'--root',str(root)])
        v04=tree(root/'generated/v04'); v04a=tree(td/'v04a'); v04b=tree(td/'v04b')
        check(v04a==v04b==v04,'v04_clean_regeneration',{'files':len(v04)},checks)

        # V05 fixed-C generation only. No compiler is invoked.
        run([sys.executable,str(root/'tools/generate_static_core.py'),'--rom',str(rom),'--output',str(td/'v05a'),'--root',str(root)])
        run([sys.executable,str(root/'tools/generate_static_core.py'),'--rom',str(rom),'--output',str(td/'v05b'),'--root',str(root)])
        v05=tree(root/'generated/v05'); v05a=tree(td/'v05a'); v05b=tree(td/'v05b')
        check(v05a==v05b==v05,'v05_clean_regeneration',{'files':len(v05)},checks)

        # V10 fixed-point generation twice from clean temporary outputs.
        run([sys.executable,str(root/'tools/analyze_v10_romwide_closure.py'),str(rom),str(td/'v10a'),'--root',str(root)])
        run([sys.executable,str(root/'tools/analyze_v10_romwide_closure.py'),str(rom),str(td/'v10b'),'--root',str(root)])
        v10=tree(root/'generated/v10'); v10a=tree(td/'v10a'); v10b=tree(td/'v10b')
        check(v10a==v10b==v10,'v10_clean_regeneration',{'files':len(v10)},checks)

    fixed=json.loads((root/'generated/v10/fixed-point-summary.json').read_text())
    exp=contract['expected_fixed_point']
    check(fixed['rom_wide_fixed_point'] and fixed['result']=='PASS','rom_wide_fixed_point',True,checks)
    check(fixed['accepted_instruction_contexts']==exp['accepted_instruction_contexts'],'context_count',fixed['accepted_instruction_contexts'],checks)
    check(fixed['accepted_instruction_bytes']==exp['code_bytes'] and fixed['inline_data_bytes']==exp['inline_data_bytes'] and fixed['metadata_bytes']==exp['metadata_bytes'] and fixed['statically_excluded_immutable_data_bytes']==exp['excluded_immutable_data_bytes'],'byte_ownership_counts',{'code':fixed['accepted_instruction_bytes'],'inline':fixed['inline_data_bytes'],'metadata':fixed['metadata_bytes'],'excluded':fixed['statically_excluded_immutable_data_bytes']},checks)
    check(fixed['unresolved_prg_bytes']==0 and fixed['unresolved_current_graph_edges']==0,'zero_unresolved',0,checks)

    mapper=json.loads((root/'generated/v10/mapper-execution-domain.json').read_text())
    check(mapper['bank_select_d6_domain']==[0],'mapper_prg_mode_d6',mapper['bank_select_d6_domain'],checks)
    check(mapper['r7_executable_values']==exp['executable_r7_values'],'r7_domain',mapper['r7_executable_values'],checks)
    check(mapper['r6_window_executable_targets']==0,'r6_nonexecutability',0,checks)

    # Generated-C production purity and fail-closed defaults.
    contracts=json.loads((root/'generated/v05/opcode-form-contracts.json').read_text())
    forms=contracts.get('forms',contracts if isinstance(contracts,list) else [])
    if isinstance(forms,dict): forms=list(forms.values())
    bad_flags=[]
    def walk(o,path=''):
        if isinstance(o,dict):
            for k,v in o.items():
                if k in ('runtime_opcode_decode','runtime_opcode_fetch') and v is not False: bad_flags.append((path+'/'+k,v))
                walk(v,path+'/'+str(k))
        elif isinstance(o,list):
            for i,v in enumerate(o): walk(v,path+f'/{i}')
    walk(contracts)
    check(not bad_flags,'opcode_form_contracts_static',{'bad':bad_flags},checks)
    generated_text='\n'.join(p.read_text(errors='ignore') for p in sorted((root/'generated/v05').glob('*.c')))
    forbidden=[r'switch\s*\(\s*opcode\s*\)',r'rt->prg_rom\s*\[',r'ExecOpcode',r'interpreter_fallback']
    hits=[pat for pat in forbidden if re.search(pat,generated_text,re.I)]
    check(not hits,'generated_no_runtime_decode_or_fetch',{'hits':hits},checks)
    bank_defaults=[]
    for n in range(16):
        t=(root/f'generated/v05/bank_{n:02d}.c').read_text()
        ok='default:' in t and 'return AI2_V05_STEP_MISSING_IDENTITY;' in t
        bank_defaults.append(ok)
    check(all(bank_defaults),'all_bank_defaults_fail_closed',{'banks':sum(bank_defaults)},checks)
    dispatch=(root/'generated/v05/bank_dispatch.c').read_text()
    check('default: return AI2_V05_STEP_MISSING_BANK;' in dispatch,'unknown_bank_fails_closed',True,checks)

    # ROM array is retained for data reads/DMC, but never used by generated code as opcode input.
    runtime=(root/'src/runtime/ai2_v06_runtime.c').read_text()
    check(runtime.count('rt->prg_rom[mapped.segment_offset]')==1,'runtime_prg_read_single_bus_site',1,checks)
    check('switch(opcode)' not in runtime and 'switch (opcode)' not in runtime,'runtime_no_opcode_switch',True,checks)

    # Static reset timing construction proof; oracle receipt only qualifies it.
    ppu=(root/'src/ppu/ai2_v07_ppu.c').read_text(); rt=runtime
    check('ppu->scanline = 0;' in ppu and 'ppu->dot = 4;' in ppu,'ppu_power_on_phase_source',{'scanline':0,'dot':4},checks)
    check('for(i = 0; i < 5u; i++)' in rt and 'ai2_v06_bus_read(rt, 0xFFFCu' in rt and 'ai2_v06_bus_read(rt, 0xFFFDu' in rt,'reset_seven_cpu_cycles_source','5 internal + 2 vector reads',checks)
    check('for(i = 0; i < 3u; i++)' in rt and 'rt->ppu_cycles++;' in rt,'cpu_to_ppu_ratio_source','3 PPU dots per CPU cycle',checks)

    required=[
      'src/mapper/ai2_mmc3_mapper.c','src/runtime/ai2_v06_runtime.c','src/ppu/ai2_v07_ppu.c','src/apu/ai2_v08_apu.c',
      'src/api/ai2_v09_core.c','include/ai2_static_core.h','generated/v05/bank_dispatch.c']
    check(all((root/p).is_file() for p in required),'required_runtime_modules_exist',required,checks)
    prod='\n'.join((root/p).read_text(errors='ignore') for p in required if p.endswith('.c') or p.endswith('.h'))
    check('Mesen' not in prod and 'mesen' not in prod,'production_has_no_oracle_dependency',True,checks)

    out={'schema':1,'milestone':'V10','result':'PASS','compile_invoked':False,'runtime_executed':False,
         'gameplay_used_as_static_authority':False,'oracle_used_as_static_authority':False,
         'checks_passed':len(checks),'checks':checks}
    if a.json_out:
        a.json_out.parent.mkdir(parents=True,exist_ok=True); a.json_out.write_text(json.dumps(out,indent=2)+'\n')
    print(json.dumps({'result':'PASS','checks_passed':len(checks),'compile_invoked':False},sort_keys=True))

if __name__=='__main__':
    try: main()
    except Exception as e:
        print(f'FAIL: {e}',file=sys.stderr); raise
