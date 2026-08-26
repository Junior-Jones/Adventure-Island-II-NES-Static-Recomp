#!/usr/bin/env python3
"""Adventure Island II V04 static control-flow / executable-RAM closure analyzer.

This tool performs offline construction from immutable ROM bytes and reviewed static
proofs. It never runs the game, consumes a gameplay trace, or admits an observed PC.
"""
from __future__ import annotations
import argparse, collections, csv, hashlib, json
from pathlib import Path

BR = {'BPL','BMI','BVC','BVS','BCC','BCS','BNE','BEQ'}
EXPECTED_ROM_SHA256 = '10dd53fd62c95e77856b09c693b3bdd14b62a89988902eea111257cafe9b8acc'
EXPECTED_PAYLOAD_SHA256 = '5ee479ef04d80366a75dd144cbeb47d0347ee82025d1b65dd358e1f6498c9a39'
EXPECTED_PRG_SHA256 = 'e021cedf0b7700b3eee7e5b76b2e23000075e06f4d7c13a4b556452952d3aec0'

def h256(b: bytes) -> str: return hashlib.sha256(b).hexdigest()
def hx(v): return '' if v is None else f'0x{v:04X}'

def load_rom(path: Path):
    raw=path.read_bytes()
    if h256(raw)!=EXPECTED_ROM_SHA256: raise SystemExit('V04 exact-ROM SHA-256 mismatch')
    if len(raw)<16 or raw[:4]!=b'NES\x1a': raise SystemExit('V04 invalid iNES image')
    payload=raw[16:]
    if len(payload)!=262144 or h256(payload)!=EXPECTED_PAYLOAD_SHA256: raise SystemExit('V04 payload mismatch')
    prg=payload[:131072]
    if h256(prg)!=EXPECTED_PRG_SHA256: raise SystemExit('V04 PRG mismatch')
    return raw,prg

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('rom',type=Path)
    ap.add_argument('output',type=Path)
    ap.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    a=ap.parse_args(); root=a.root.resolve(); out=a.output.resolve(); out.mkdir(parents=True,exist_ok=True)
    raw,rom=load_rom(a.rom)
    entries=json.loads((root/'config/2a03-opcodes.json').read_text())['entries']
    optab={e['opcode']:e for e in entries}
    proofs=json.loads((root/'config/v04-static-proofs.json').read_text())
    overrides={(x['source_bank'],int(x['source_pc'],16),int(x['target_pc'],16)):x['target_bank'] for x in proofs['cross_window_transfers']}

    def meta_at(bank,pc):
        off=bank*0x2000+(pc&0x1fff); op=rom[off]; e=optab[op]; size=e['size']; return e,rom[off:off+size]
    def target(bs,pc,mn,mode):
        if mn in BR:
            r=bs[1] if bs[1]<0x80 else bs[1]-256; return (pc+2+r)&0xffff
        if mn in ('JMP','JSR') and mode=='Abs': return bs[1]|(bs[2]<<8)
        return None
    def map_target(sb,sp,t):
        if (sb,sp,t) in overrides: return overrides[(sb,sp,t)]
        if 0xE000<=t<=0xFFFF:return 15
        if 0xC000<=t<=0xDFFF:return 14
        if 0xA000<=t<=0xBFFF:
            return sb if sb in (10,11,12,13) and 0xA000<=sp<=0xBFFF else None
        if 0x8000<=t<=0x9FFF:return None
        return -1
    def words(bank,start,count):
        vals=[]
        for i in range(count):
            o=bank*0x2000+((start&0x1fff)+2*i); vals.append(rom[o]|(rom[o+1]<<8))
        return vals

    indirect={}
    for d in proofs['indirect_dispatchers']:
        b=d['physical_bank']; pc=int(d['cpu_pc'],16); st=int(d['table_start'],16); n=d['entry_count']
        e,bs=meta_at(b,pc)
        if e['mnemonic']!='JMP' or e['mode']!='Ind': raise SystemExit(f'V04 dispatcher instruction mismatch {b}:{pc:04X}')
        indirect[(b,pc)]=words(b,st,n)
    # Immutable-table anchors that make accidental count drift fail closed.
    if indirect[(15,0xF27C)][0]!=0xF299 or 0xF299 != 0xF27F+13*2: raise SystemExit('F27C table structure mismatch')
    if indirect[(15,0xF2F3)][0]!=0xF322 or 0xF322 != 0xF2F6+22*2: raise SystemExit('F2F3 table structure mismatch')
    if indirect[(10,0xABFB)][0]!=0xAC32 or 0xAC32 != 0xABFE+26*2: raise SystemExit('ABFB table structure mismatch')
    if len(indirect[(11,0xA3E6)])!=96 or len(indirect[(12,0xA1BB)])!=128: raise SystemExit('large dispatcher table mismatch')

    def inline_dispatch(bank,callpc):
        st=(callpc+3)&0xffff; off=bank*0x2000+(st&0x1fff); first=rom[off]|rom[off+1]<<8
        diff=first-st
        if diff<=0 or diff%2 or diff//2>64: return None
        n=diff//2; vals=words(bank,st,n); return st,n,vals

    q=collections.deque((d['physical_bank'],int(d['cpu_pc'],16),d['kind']) for d in proofs['roots'])
    inst={}; edges=[]; unresolved=[]; overlaps=[]; stack_tables=[]; inline=set(); continuations=[]
    while q:
        bank,pc,origin=q.popleft(); ident=(bank,pc)
        if ident in inst: continue
        if bank==15 and not 0xE000<=pc<=0xFFFF: unresolved.append((bank,pc,'wrong-window',None,origin)); continue
        if bank==14 and not 0xC000<=pc<=0xDFFF: unresolved.append((bank,pc,'wrong-window',None,origin)); continue
        if bank in (10,11,12,13) and not 0xA000<=pc<=0xBFFF: unresolved.append((bank,pc,'wrong-window',None,origin)); continue
        if bank not in range(16) or pc<0x8000: unresolved.append((bank,pc,'non-ROM-entry',None,origin)); continue
        e,bs=meta_at(bank,pc); sz=e['size']; mn=e['mnemonic']; mode=e['mode']
        for aa in range(pc,pc+sz):
            if (bank,aa) in inline: overlaps.append((bank,pc,aa,'instruction-over-inline-data'))
        inst[ident]={'size':sz,'mnemonic':mn,'mode':mode,'bytes':bs.hex(),'origin':origin}
        nxt=(pc+sz)&0xffff
        def add(kind,t,tb=None,reason=''):
            if tb is None: tb=map_target(bank,pc,t)
            edges.append((bank,pc,kind,tb,t,reason))
            if tb is None or tb==-1: unresolved.append((bank,pc,kind,t,reason))
            else:q.append((tb,t,f'{bank}:{pc:04X}:{kind}'))
        if mn in BR:
            add('branch',target(bs,pc,mn,mode)); edges.append((bank,pc,'fallthrough',bank,nxt,'')); q.append((bank,nxt,origin))
        elif mn=='JSR':
            t=target(bs,pc,mn,mode)
            if t in (0xEDA3,0xFF84):
                add('call-stack-dispatcher',t,15)
                z=inline_dispatch(bank,pc)
                if not z: unresolved.append((bank,pc,'stack-table-parse',t,'')); continue
                st,n,vals=z; stack_tables.append((bank,pc,t,st,n,vals))
                for aa in range(st,st+2*n): inline.add((bank,aa))
                for i,tt in enumerate(vals):
                    tb=map_target(bank,pc,tt); edges.append((bank,pc,'stack-dispatch-target',tb,tt,f'entry={i}'))
                    if tb is None or tb==-1: unresolved.append((bank,pc,'stack-dispatch-target',tt,f'entry={i}'))
                    else:q.append((tb,tt,f'stack-dispatch {bank}:{pc:04X}[{i}]'))
            elif t==0xFF9C:
                add('call-return-adjuster',t,15)
                for aa in range(pc+3,pc+9):inline.add((bank,aa))
                adj=(pc+9)&0xffff; continuations.append((bank,pc,adj,'adjusted+6'))
                edges.append((bank,pc,'adjusted-return-continuation',bank,adj,'EDC1/EDE2 consumes six inline bytes'));q.append((bank,adj,origin))
            else:
                add('call',t); continuations.append((bank,pc,nxt,'normal'))
                edges.append((bank,pc,'post-return-continuation',bank,nxt,''));q.append((bank,nxt,origin))
        elif mn=='JMP':
            if mode=='Abs': add('jump',target(bs,pc,mn,mode))
            elif (bank,pc)==(15,0xEDB6): edges.append((bank,pc,'stack-dispatch-internal',None,None,'targets are proven per accepted caller inline table'))
            elif (bank,pc) in indirect:
                for i,t in enumerate(indirect[(bank,pc)]): add('indirect-jump-proven',t,reason=f'entry={i}')
            else: unresolved.append((bank,pc,'indirect-jump',None,''));edges.append((bank,pc,'indirect-jump',None,None,''))
        elif mn=='RTS':edges.append((bank,pc,'return',None,None,'resolved by accepted call continuation relation'))
        elif mn=='RTI':edges.append((bank,pc,'interrupt-return',None,None,'returns to interrupted accepted context; no new ROM target allowlist'))
        elif mn=='BRK':add('brk-vector',0xE152,15)
        else:edges.append((bank,pc,'fallthrough',bank,nxt,''));q.append((bank,nxt,origin))

    # Cross-proof anchors for the mapper-value set, including the V04 bank-12 discovery.
    anchors={
      0xF2CF:'a207a90a8de70720f4e160', 0xF2DA:'a207a90b8de70720f4e160',
      0xDFE4:'a207a90c206fff', 0xF382:'a207a90d20f4e1'
    }
    for pc,hexprefix in anchors.items():
        b=14 if pc==0xDFE4 else 15; off=b*0x2000+(pc&0x1fff)
        if not rom[off:off+len(bytes.fromhex(hexprefix))].hex().startswith(hexprefix): raise SystemExit(f'mapper proof anchor mismatch at {b}:{pc:04X}')

    if len(stack_tables)!=proofs['stack_dispatch']['expected_callsites'] or sum(x[4] for x in stack_tables)!=proofs['stack_dispatch']['expected_entries']:
        raise SystemExit('stack-dispatch callsite/entry proof mismatch')
    if unresolved or overlaps: raise SystemExit(f'V04 closure failed: unresolved={len(unresolved)} overlaps={len(overlaps)}')
    indirect_count=sum(len(v) for v in indirect.values())+sum(x[4] for x in stack_tables)
    if indirect_count!=404: raise SystemExit(f'expected 404 proven indirect targets, got {indirect_count}')
    if len(inst)!=16544 or sum(v['size'] for v in inst.values())!=35423 or len(continuations)!=1133:
        raise SystemExit('V04 reconstructed authority counts changed unexpectedly')
    # No current-graph target enters CPU RAM; no accepted instruction byte is writable because every accepted context is PRG ROM.
    ram_targets=[e for e in edges if e[4] is not None and e[4]<0x8000]
    if ram_targets: raise SystemExit('unexpected executable-RAM target in current graph')

    def write_csv(path,header,rows):
        with path.open('w',newline='',encoding='utf-8') as f:
            w=csv.writer(f,lineterminator='\n');w.writerow(header);w.writerows(rows)
    write_csv(out/'instructions.csv',['physical_bank','cpu_pc','size','mnemonic','mode','bytes','origin'],
              [[b,f'0x{p:04X}',v['size'],v['mnemonic'],v['mode'],v['bytes'],v['origin']] for (b,p),v in sorted(inst.items())])
    write_csv(out/'edges.csv',['source_bank','source_pc','kind','target_bank','target_pc','reason'],
              [[b,f'0x{p:04X}',k,'' if tb is None else tb,'' if t is None else f'0x{t:04X}',r] for b,p,k,tb,t,r in sorted(edges,key=lambda x:(x[0],x[1],x[2],-1 if x[3] is None else x[3],-1 if x[4] is None else x[4],x[5]))])
    indrows=[]
    for (b,p),vals in sorted(indirect.items()):
        for i,t in enumerate(vals): indrows.append([b,f'0x{p:04X}','word-table',i,map_target(b,p,t),f'0x{t:04X}'])
    for b,p,t,st,n,vals in sorted(stack_tables):
        for i,tt in enumerate(vals): indrows.append([b,f'0x{p:04X}','inline-stack-table',i,map_target(b,p,tt),f'0x{tt:04X}'])
    write_csv(out/'indirect-targets.csv',['source_bank','source_pc','kind','entry','target_bank','target_pc'],indrows)
    write_csv(out/'return-continuations.csv',['source_bank','jsr_pc','continuation_pc','kind'],[[b,f'0x{p:04X}',f'0x{c:04X}',k] for b,p,c,k in sorted(continuations)])
    write_csv(out/'inline-data.csv',['physical_bank','start_pc','byte_count','kind'],[[b,f'0x{st:04X}',2*n,'stack-dispatch-table'] for b,p,t,st,n,vals in sorted(stack_tables)]+[[11,'0xB9E6',6,'EDC1/EDE2 inline parameters']])
    write_csv(out/'unresolved-edges.csv',['source_bank','source_pc','kind','target_pc','reason'],[])
    (out/'executable-ram-epochs.json').write_text(json.dumps({'schema':1,'milestone':'V04','current_graph_epochs':[], 'current_graph_executable_ram_targets':0,'current_graph_rom_to_ram_executable_copies':0,'current_graph_self_modifying_code':0,'rom_wide_exclusion_claimed':False,'policy':proofs['ram_execution_policy']},indent=2)+'\n')
    summary={
      'schema':1,'milestone':'V04','construction_method':'offline static exact-ROM analysis only; no gameplay trace inputs',
      'rom_sha256':h256(raw),'payload_sha256':EXPECTED_PAYLOAD_SHA256,'prg_sha256':h256(rom),'accepted_instruction_contexts':len(inst),'accepted_instruction_bytes':sum(v['size'] for v in inst.values()),
      'edge_records':len(edges),'return_continuations':len(continuations),'proven_indirect_targets':indirect_count,'stack_dispatch_callsites':len(stack_tables),'stack_dispatch_entries':sum(x[4] for x in stack_tables),
      'executable_r7_values':proofs['expected_executable_r7_values'],'unresolved_current_graph_edges':0,'decode_or_inline_overlaps':0,
      'current_graph_executable_ram_epochs':0,'current_graph_rom_to_ram_executable_copies':0,'current_graph_self_modifying_code':0,
      'rom_wide_closure_claimed':False,'rom_wide_statement':'V04 closes every accepted current-graph edge. Bytes not admitted by the current static graph remain subject to later ROM-wide closure; V04 does not convert absence of current reachability into a whole-ROM impossibility claim.'
    }
    (out/'closure-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    # Hash all generated V04 artifacts except this manifest itself.
    files=[]
    for p in sorted(out.iterdir()):
        if p.is_file() and p.name!='generated-manifest.json':files.append({'path':p.name,'bytes':p.stat().st_size,'sha256':h256(p.read_bytes())})
    canonical=''.join(f"{x['sha256']}  {x['bytes']}  {x['path']}\n" for x in files).encode()
    (out/'generated-manifest.json').write_text(json.dumps({'schema':1,'files':files,'aggregate_sha256':h256(canonical)},indent=2)+'\n')
    print(f"PASS V04 static closure: instructions={len(inst)} bytes={sum(v['size'] for v in inst.values())} edges={len(edges)} continuations={len(continuations)} indirect_targets={indirect_count}")
    print('PASS V04 current graph: unresolved=0 overlaps=0 executable_ram_epochs=0 gameplay_seeds=0')
    return 0
if __name__=='__main__':raise SystemExit(main())
