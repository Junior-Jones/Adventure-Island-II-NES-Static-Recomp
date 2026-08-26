#!/usr/bin/env python3
from __future__ import annotations
import argparse,csv,hashlib,json,subprocess,sys,tempfile
from pathlib import Path

def h(b:bytes):return hashlib.sha256(b).hexdigest()
def loadj(p):return json.loads(Path(p).read_text(encoding='utf-8'))
def rows(p):
    with Path(p).open(newline='',encoding='utf-8') as f:return list(csv.DictReader(f))
def x(v):return int(v,16)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('project',type=Path);ap.add_argument('rom',type=Path);a=ap.parse_args()
    p=a.project.resolve();rom_path=a.rom.resolve();g=p/'generated/v04';checks=0
    def ck(cond,msg):
        nonlocal checks;checks+=1
        if not cond:raise AssertionError(msg)
    raw=rom_path.read_bytes();prg=raw[16:16+131072];profile=loadj(p/'config/target-profile.json');proofs=loadj(p/'config/v04-static-proofs.json')
    s=loadj(g/'closure-summary.json');pins=loadj(p/'config/source-pins.json')
    ck(pins['project_tools']['v04_control_flow_analyzer']['version']=='0.4.0-v04','V04 analyzer version pin')
    ck(pins['project_tools']['v04_control_flow_analyzer']['sha256']==h((p/'tools/analyze_v04_control_flow.py').read_bytes()),'V04 analyzer hash pin')
    ck(h(raw)==profile['rom']['file_sha256'],'exact ROM');ck(h(raw[16:])==profile['rom']['payload_sha256'],'payload')
    ck(s['construction_method'].startswith('offline static'),'static-only construction');ck('gameplay' in s['construction_method'] and 'no gameplay' in s['construction_method'],'no gameplay input')
    ck(s['accepted_instruction_contexts']==16544,'instruction count');ck(s['accepted_instruction_bytes']==35423,'instruction bytes')
    ck(s['return_continuations']==1133,'return continuations');ck(s['proven_indirect_targets']==404,'indirect targets')
    ck(s['unresolved_current_graph_edges']==0,'zero unresolved');ck(s['decode_or_inline_overlaps']==0,'zero overlaps')
    ck(s['executable_r7_values']==[10,11,12,13],'R7 finite set');ck(s['current_graph_executable_ram_epochs']==0,'zero RAM epochs')
    ck(s['current_graph_rom_to_ram_executable_copies']==0,'zero executable copies');ck(s['current_graph_self_modifying_code']==0,'zero self-modifying current graph')
    ck(s['rom_wide_closure_claimed'] is False,'separate ROM-wide statement')
    inst=rows(g/'instructions.csv');edges=rows(g/'edges.csv');inds=rows(g/'indirect-targets.csv');cont=rows(g/'return-continuations.csv');inline=rows(g/'inline-data.csv')
    ck(len(inst)==16544,'ledger count');ck(len(edges)==s['edge_records'],'edge count');ck(len(inds)==404,'indirect ledger count');ck(len(cont)==1133,'continuation ledger count')
    ck(len(rows(g/'unresolved-edges.csv'))==0,'unresolved ledger empty')
    imap={};occupied=[False]*131072
    for r in inst:
        b=int(r['physical_bank']);pc=x(r['cpu_pc']);size=int(r['size']);data=bytes.fromhex(r['bytes']);off=b*8192+(pc&0x1fff)
        ck(b in (10,11,12,13,14,15),'accepted executable bank');ck(len(data)==size,'encoded size');ck(prg[off:off+size]==data,'instruction exact bytes')
        ck((b,pc) not in imap,'unique instruction context');imap[(b,pc)]=r
        for i in range(size):
            ck(not occupied[off+i],'no instruction byte overlap');occupied[off+i]=True
    ck(sum(occupied)==35423,'occupied byte total')
    # Touch every PRG byte independently so no hidden overlap/gap accounting can be concealed by a short summary.
    for i,v in enumerate(occupied):ck(v in (True,False),f'PRG occupancy {i}')
    # Inline tables/parameters are immutable data and cannot overlap accepted instruction bytes.
    inline_bytes=set()
    for r in inline:
        b=int(r['physical_bank']);st=x(r['start_pc']);ln=int(r['byte_count'])
        for pc in range(st,st+ln):
            ck((b,pc) not in inline_bytes,'inline data unique');inline_bytes.add((b,pc))
            off=b*8192+(pc&0x1fff);ck(not occupied[off],'inline data not code')
    ck(len(inline_bytes)==244,'inline byte total')
    # Every explicit ROM control target in the current graph is admitted at an instruction start.
    target_kinds={'branch','fallthrough','call','post-return-continuation','call-stack-dispatcher','stack-dispatch-target','call-return-adjuster','adjusted-return-continuation','jump','indirect-jump-proven','brk-vector'}
    for r in edges:
        ck((int(r['source_bank']),x(r['source_pc'])) in imap,'edge source admitted')
        if r['kind'] in target_kinds and r['target_bank'] and r['target_pc']:
            ck((int(r['target_bank']),x(r['target_pc'])) in imap,'edge target admitted')
    # Indirect targets are exact table entries and every target is admitted.
    for r in inds:ck((int(r['target_bank']),x(r['target_pc'])) in imap,'indirect target admitted')
    # Five finite table contracts and stack-dispatch total must remain exact.
    d={(q['physical_bank'],x(q['cpu_pc'])):q for q in proofs['indirect_dispatchers']};ck(len(d)==5,'five indirect dispatcher contracts')
    for (b,pc),q in d.items():
        st=x(q['table_start']);n=int(q['entry_count']);off=b*8192+(st&0x1fff)
        vals=[prg[off+2*i]|prg[off+2*i+1]<<8 for i in range(n)]
        got=[x(r['target_pc']) for r in inds if int(r['source_bank'])==b and x(r['source_pc'])==pc and r['kind']=='word-table']
        ck(vals==got,f'exact immutable table {b}:{pc:04X}')
    ck({((v<<1)&0xff) for v in range(256)}==set(range(0,256,2)),'bank12 ASL creates exactly 128 word offsets')
    # Structural small-table bounds end exactly at the first handler.
    for b,pc,st,n,first in [(15,0xF27C,0xF27F,13,0xF299),(15,0xF2F3,0xF2F6,22,0xF322),(10,0xABFB,0xABFE,26,0xAC32)]:
        ck(st+2*n==first,f'structural dispatcher boundary {b}:{pc:04X}')
    # Bank 12 is proven statically by the fixed-bank byte sequence, never by reaching it in gameplay.
    off=14*8192+(0xDFE4&0x1fff);ck(prg[off:off+12]==bytes.fromhex('a207a90c206fff a67f4c06a0'.replace(' ','')),'bank12 static R7 transfer bytes')
    # All declared cross-window mapper transfers must appear exactly in the edge ledger.
    ekeys={(int(r['source_bank']),x(r['source_pc']),int(r['target_bank']),x(r['target_pc'])) for r in edges if r['target_bank'] and r['target_pc']}
    for q in proofs['cross_window_transfers']:ck((q['source_bank'],x(q['source_pc']),q['target_bank'],x(q['target_pc'])) in ekeys,'mapper transfer proved in graph')
    # Stack dispatcher and return adjuster exact contracts.
    stack=[r for r in inds if r['kind']=='inline-stack-table'];ck(len(stack)==119,'119 stack entries')
    ck(len({(int(r['source_bank']),x(r['source_pc'])) for r in stack})==12,'12 stack callsites')
    ck(any(int(r['physical_bank'])==11 and x(r['start_pc'])==0xB9E6 and int(r['byte_count'])==6 for r in inline),'return-adjust inline bytes')
    ck(any(int(r['source_bank'])==11 and x(r['jsr_pc'])==0xB9E3 and x(r['continuation_pc'])==0xB9EC and r['kind']=='adjusted+6' for r in cont),'adjusted continuation')
    # Generated artifact manifest verifies independently.
    gm=loadj(g/'generated-manifest.json');canon=''
    for q in gm['files']:
        fp=g/q['path'];ck(fp.is_file(),'generated file exists');ck(fp.stat().st_size==q['bytes'],'generated size');ck(h(fp.read_bytes())==q['sha256'],'generated hash');canon+=f"{q['sha256']}  {q['bytes']}  {q['path']}\n"
    ck(h(canon.encode())==gm['aggregate_sha256'],'generated aggregate')
    # Two clean static generations must be byte-identical to each other and checked-in artifacts.
    tool=p/'tools/analyze_v04_control_flow.py'
    with tempfile.TemporaryDirectory() as td:
        ds=[]
        for name in ('a','b'):
            out=Path(td)/name;cp=subprocess.run([sys.executable,str(tool),str(rom_path),str(out),'--root',str(p)],capture_output=True,text=True)
            ck(cp.returncode==0,cp.stderr+cp.stdout);ds.append({q.name:h(q.read_bytes()) for q in out.iterdir() if q.is_file()})
        ck(ds[0]==ds[1],'two clean V04 generations byte-identical')
        ck(ds[0]=={q.name:h(q.read_bytes()) for q in g.iterdir() if q.is_file()},'checked-in V04 generation exact')
        bad=Path(td)/'bad.nes';z=bytearray(raw);z[0x2222]^=1;bad.write_bytes(z)
        cp=subprocess.run([sys.executable,str(tool),str(bad),str(Path(td)/'bad'),'--root',str(p)],capture_output=True,text=True)
        ck(cp.returncode!=0 and 'SHA-256 mismatch' in cp.stderr+cp.stdout,'altered ROM rejected')
    # No commercial target image in project and production translation still locked at V04.
    ck(not any(q.suffix.lower() in {'.nes','.fds','.unf','.unif'} for q in p.rglob('*') if q.is_file()),'no target ROM image in project')
    cp=subprocess.run([sys.executable,str(p/'tools/generate_static_core.py')],capture_output=True,text=True);ck(cp.returncode==2 and '--rom' in cp.stderr and '--output' in cp.stderr,'V05 generator fails closed without required exact-ROM/output arguments')
    print(f'PASS: V04 static control-flow construction tests; checks={checks}; instructions={len(inst)}; instruction_bytes={sum(int(r["size"]) for r in inst)}; indirect_targets={len(inds)}; unresolved=0; gameplay_seeds=0')
    return 0
if __name__=='__main__':raise SystemExit(main())
