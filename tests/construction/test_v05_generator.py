#!/usr/bin/env python3
from __future__ import annotations

import csv
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ROM = Path(os.environ.get('AI2_ROM', '/mnt/data/Adventure Island II (USA).nes'))
GEN = ROOT / 'generated/v05'
TOOL = ROOT / 'tools/generate_static_core.py'
CHECKS = 0


def check(cond: bool, msg: str) -> None:
    global CHECKS
    CHECKS += 1
    if not cond:
        raise AssertionError(msg)


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tree_hashes(root: Path) -> dict[str, str]:
    return {p.relative_to(root).as_posix(): sha(p) for p in sorted(root.rglob('*')) if p.is_file()}


def read_csv(path: Path):
    with path.open(newline='', encoding='utf-8') as f:
        return list(csv.DictReader(f))


def run(cmd, **kw):
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kw)


def verify_manifest(root: Path) -> None:
    m = json.loads((root/'generated-tree-manifest.json').read_text())
    paths=[]
    agg=hashlib.sha256()
    for item in m['files']:
        p=root/item['path']; paths.append(item['path'])
        check(p.is_file(), f"manifest missing {item['path']}")
        data=p.read_bytes()
        check(len(data)==item['bytes'], f"size mismatch {item['path']}")
        h=hashlib.sha256(data).hexdigest()
        check(h==item['sha256'], f"hash mismatch {item['path']}")
        agg.update(item['path'].encode()); agg.update(b'\0'); agg.update(h.encode()); agg.update(b'\n')
    actual=sorted(p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() and p.name!='generated-tree-manifest.json')
    check(actual==paths, 'manifest path set mismatch')
    check(agg.hexdigest()==m['aggregate_sha256'], 'aggregate mismatch')


def main() -> int:
    check(ROM.is_file(), 'external ROM missing')
    exact=hashlib.sha256(ROM.read_bytes()).hexdigest()
    check(exact=='10dd53fd62c95e77856b09c693b3bdd14b62a89988902eea111257cafe9b8acc','ROM identity mismatch')

    inst=read_csv(ROOT/'generated/v04/instructions.csv')
    edges=read_csv(ROOT/'generated/v04/edges.csv')
    indirect=read_csv(ROOT/'generated/v04/indirect-targets.csv')
    returns=read_csv(ROOT/'generated/v04/return-continuations.csv')
    closure=json.loads((ROOT/'generated/v04/closure-summary.json').read_text())
    check(len(inst)==16544,'V04 instruction count')
    check(len(indirect)==404,'V04 indirect count')
    check(len(returns)==1133,'V04 return count')
    check(closure['unresolved_current_graph_edges']==0,'V04 unresolved edges')
    check(closure['rom_wide_closure_claimed'] is False,'V04 must not claim ROM-wide closure')

    identity=json.loads((GEN/'generated-identity.json').read_text())
    check(identity['accepted_instruction_contexts']==16544,'generated identity count')
    check(identity['opcode_form_count']==99,'opcode form count')
    check(identity['proven_indirect_entries']==404,'generated indirect count')
    check(identity['return_continuation_rows']==1133,'generated return rows')
    check(identity['rom_wide_closure_claimed'] is False,'V05 must preserve truthful closure scope')
    expected_banks={10:2072,11:2691,12:2925,13:2935,14:2609,15:3312}
    for bank in range(16):
        check(int(identity['per_physical_prg_bank'][str(bank)])==expected_banks.get(bank,0),f'bank {bank} identity count')

    pins=json.loads((ROOT/'config/source-pins.json').read_text())
    check(pins['project_tools']['generator_entry']['version']=='0.5.1-v05-compact','generator version pin')
    check(pins['project_tools']['generator_entry']['sha256']==sha(TOOL),'generator source hash pin')
    check(pins['starter_nes']['active_uploaded_package']=='Starter NES(4).zip','active Starter package pin')
    starter=ROOT.parents[1]/'Reference/Starter NES'
    check(starter.is_dir(),'active Starter unpacked')
    check(pins['starter_nes']['cold_boot_static_closure_guide_sha256']==sha(starter/'Docs/ROM TO COLD BOOT AND STATIC CLOSURE.md'),'cold-boot guide hash pin')

    receipt=json.loads((GEN/'generation-receipt.json').read_text())
    check(receipt['tool']['version']=='0.5.1-v05-compact','generator version receipt')
    for k in ['runtime_opcode_fetch','runtime_opcode_decode','interpreter','emulator_fallback']:
        check(receipt['execution_policy'][k] is False,f'policy {k}')
    check(receipt['normalized_arguments']==['--rom','<EXTERNAL_ROM>','--output','<OUTPUT>','--root','<PROJECT_ROOT>'],'normalized args')

    verify_manifest(GEN)

    # Exact compact descriptor/helper mapping checks.
    bank_text={bank:(GEN/f'bank_{bank:02d}.c').read_text() for bank in range(16)}
    descriptor_counts={bank:len(re.findall(r'^\s*\{ai2_v05_op_[A-Za-z0-9_]+, 0x[0-9A-F]{4}u,',bank_text[bank],re.M)) for bank in range(16)}
    for bank in range(16):
        check(descriptor_counts[bank]==expected_banks.get(bank,0),f'bank {bank} descriptor count')
    for row in inst:
        bank=int(row['physical_bank']); pc=int(row['cpu_pc'],16); op=int(row['bytes'][:2],16)
        mode=''.join(ch if ch.isalnum() else '_' for ch in row['mode'])
        helper=f'ai2_v05_op_{op:02X}_{row["mnemonic"]}_{mode}'
        marker=f'{{{helper}, 0x{pc:04X}u,'
        check(marker in bank_text[bank],f'missing compact descriptor {bank}:{pc:04X}')

    # No generated C source may read ROM/opcode bytes or dispatch on a fetched opcode.
    generated_c='\n'.join((GEN/p).read_text() for p in [f'bank_{b:02d}.c' for b in range(16)]+['bank_dispatch.c','indirect_domains.c','return_domains.c'])
    forbidden=[r'\brom\s*\[',r'\bread_opcode\s*\(',r'\bdecode_opcode\s*\(',r'switch\s*\([^)]*opcode',r'ExecOpcode',r'interpreter\s*\(']
    for pat in forbidden:
        check(re.search(pat,generated_c,re.I) is None,f'forbidden runtime pattern {pat}')
    check('AI2_V05_STEP_MISSING_IDENTITY' in generated_c,'missing identity trap absent')
    check('AI2_V05_STEP_BAD_DYNAMIC_TARGET' in generated_c,'dynamic target trap absent')

    contracts=json.loads((GEN/'opcode-form-contracts.json').read_text())
    check(contracts['form_count']==99,'contract form count')
    for form in contracts['forms']:
        check(form['runtime_opcode_fetch'] is False,'contract runtime fetch')
        check(form['runtime_opcode_decode'] is False,'contract runtime decode')
        check(len(form['bus_sequence_contract'])>0,'empty bus sequence contract')
    # Explicit RMW old/new write contract and indexed store dummy-read contract must exist.
    rmw=[f for f in contracts['forms'] if f['mnemonic'] in {'ASL','LSR','ROL','ROR','INC','DEC'} and f['mode']!='Acc']
    for f in rmw:
        check('write-old' in f['bus_sequence_contract'] and 'write-new' in f['bus_sequence_contract'],f'RMW ordering {f}')
    indexed_store=[f for f in contracts['forms'] if f['mnemonic'] in {'STA','STX','STY'} and f['mode'] in {'AbsXW','AbsYW','IndYW'}]
    for f in indexed_store:
        check('indexed-dummy-read' in f['bus_sequence_contract'],f'indexed store ordering {f}')

    # Finite dynamic domains: every V04 row must be materialized in generated domain source.
    itext=(GEN/'indirect_domains.c').read_text()
    for row in indirect:
        ob=int(row['source_bank']); op=int(row['source_pc'],16); tb=int(row['target_bank']); tp=int(row['target_pc'],16)
        check(f'0x{ob:02X}u, 0x{op:04X}u' in itext,f'indirect origin {ob}:{op:04X}')
        check(f'0x{tb:02X}u, 0x{tp:04X}u' in itext,f'indirect target {tb}:{tp:04X}')
    rtext=(GEN/'return_domains.c').read_text()
    for row in returns:
        b=int(row['source_bank']); pc=int(row['continuation_pc'],16)
        check(f'0x{b:02X}u, 0x{pc:04X}u' in rtext,f'return identity {b}:{pc:04X}')

    # Strict C syntax under two compilers.
    cfiles=[str(p) for p in sorted(GEN.glob('*.c'))]
    for compiler in ['gcc','clang']:
        exe=shutil.which(compiler)
        if exe:
            proc=run([exe,'-std=c11','-Wall','-Wextra','-Werror',f'-I{GEN}','-fsyntax-only',*cfiles])
            check(proc.returncode==0,f'{compiler} strict syntax failed:\n{proc.stdout[:2000]}')

    # Two totally clean generations must be byte-identical and equal the checked-in tree.
    with tempfile.TemporaryDirectory(prefix='ai2-v05-a-') as a, tempfile.TemporaryDirectory(prefix='ai2-v05-b-') as b:
        pa=Path(a)/'out'; pb=Path(b)/'out'
        for dest in [pa,pb]:
            proc=run([sys.executable,str(TOOL),'--rom',str(ROM),'--output',str(dest),'--root',str(ROOT)])
            check(proc.returncode==0,f'clean generation failed: {proc.stdout}')
        ha=tree_hashes(pa); hb=tree_hashes(pb); hc=tree_hashes(GEN)
        check(ha==hb,'two clean generations differ')
        check(ha==hc,'checked-in generated tree differs from clean generation')

    # Modified ROM must fail before production output can be accepted.
    with tempfile.TemporaryDirectory(prefix='ai2-v05-badrom-') as t:
        bad=Path(t)/'bad.nes'; data=bytearray(ROM.read_bytes()); data[0x1234]^=1; bad.write_bytes(data)
        out=Path(t)/'out'
        proc=run([sys.executable,str(TOOL),'--rom',str(bad),'--output',str(out),'--root',str(ROOT)])
        check(proc.returncode!=0,'altered ROM accepted')
        check(not out.exists(),'altered ROM produced output tree')

    # No target ROM or cache/build residue in generated tree.
    for p in GEN.rglob('*'):
        if p.is_file():
            check(p.suffix.lower() not in {'.nes','.fds','.unf','.unif'},f'ROM-like output {p}')
            check('__pycache__' not in p.parts and p.suffix not in {'.o','.obj'},f'build residue {p}')

    print(f'V05_GENERATOR_TEST PASS checks={CHECKS}')
    return 0

if __name__=='__main__':
    raise SystemExit(main())
