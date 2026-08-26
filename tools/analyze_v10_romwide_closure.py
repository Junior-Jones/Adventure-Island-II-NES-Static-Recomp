#!/usr/bin/env python3
"""Adventure Island II V10 ROM-wide static fixed-point closure proof.

Construction authority is limited to the exact ROM, the sealed V04 graph/proof
artifacts, and architectural mapper rules.  No gameplay trace, emulator PC,
save-state, screenshot, or runtime-discovered target is accepted as authority.
"""
from __future__ import annotations
import argparse, csv, hashlib, json
from pathlib import Path

EXPECTED_ROM_SHA256='10dd53fd62c95e77856b09c693b3bdd14b62a89988902eea111257cafe9b8acc'
EXPECTED_PAYLOAD_SHA256='5ee479ef04d80366a75dd144cbeb47d0347ee82025d1b65dd358e1f6498c9a39'
EXPECTED_PRG_SHA256='e021cedf0b7700b3eee7e5b76b2e23000075e06f4d7c13a4b556452952d3aec0'
PRG_BYTES=131072
BANK_BYTES=0x2000
WRITE_MNEMONICS={'STA','STX','STY','INC','DEC','ASL','LSR','ROL','ROR'}


def sha_bytes(b: bytes)->str: return hashlib.sha256(b).hexdigest()
def sha_file(p: Path)->str: return sha_bytes(p.read_bytes())
def u16(bs: bytes)->int: return bs[1] | (bs[2]<<8)
def write_json(path: Path, obj): path.write_text(json.dumps(obj,indent=2,sort_keys=False)+'\n')

def load_rom(path: Path):
    raw=path.read_bytes()
    if sha_bytes(raw)!=EXPECTED_ROM_SHA256: raise SystemExit('V10 exact-ROM SHA-256 mismatch')
    if len(raw)!=262160 or raw[:4]!=b'NES\x1a': raise SystemExit('V10 exact iNES geometry/header mismatch')
    payload=raw[16:]
    if sha_bytes(payload)!=EXPECTED_PAYLOAD_SHA256: raise SystemExit('V10 payload SHA-256 mismatch')
    prg=payload[:PRG_BYTES]
    if sha_bytes(prg)!=EXPECTED_PRG_SHA256: raise SystemExit('V10 PRG SHA-256 mismatch')
    return raw,payload,prg

def rows(path: Path):
    with path.open(newline='') as f: return list(csv.DictReader(f))

def pcmap(inst):
    d={}
    for r in inst:
        key=(int(r['physical_bank']),int(r['cpu_pc'],16))
        if key in d: raise SystemExit(f'duplicate V04 instruction identity {key}')
        d[key]=r
    return d

def exact_sequence(inst_by_pc, bank, seq):
    for pc,mn,mode,hx in seq:
        r=inst_by_pc.get((bank,pc))
        if not r or (r['mnemonic'],r['mode'],r['bytes'])!=(mn,mode,hx):
            raise SystemExit(f'V10 immutable sequence mismatch {bank}:{pc:04X}')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('rom',type=Path)
    ap.add_argument('output',type=Path)
    ap.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    a=ap.parse_args(); root=a.root.resolve(); out=a.output.resolve(); out.mkdir(parents=True,exist_ok=True)
    raw,payload,prg=load_rom(a.rom)
    v04=root/'generated/v04'
    inst=rows(v04/'instructions.csv'); edges=rows(v04/'edges.csv'); inline=rows(v04/'inline-data.csv')
    unresolved=rows(v04/'unresolved-edges.csv')
    summary=json.loads((v04/'closure-summary.json').read_text())
    proofs=json.loads((root/'config/v04-static-proofs.json').read_text())
    mapper=json.loads((root/'config/mapper-contract.json').read_text())
    bypc=pcmap(inst)

    if summary['accepted_instruction_contexts']!=len(inst): raise SystemExit('V04 context count drift')
    if summary['accepted_instruction_bytes']!=sum(int(r['size']) for r in inst): raise SystemExit('V04 code-byte count drift')
    if summary['unresolved_current_graph_edges']!=0 or unresolved: raise SystemExit('V04 graph no longer closed')
    if summary['decode_or_inline_overlaps']!=0: raise SystemExit('V04 decode/inline overlap is nonzero')
    if summary['current_graph_executable_ram_epochs']!=0 or summary['current_graph_self_modifying_code']!=0: raise SystemExit('V04 executable RAM/SMC closure drift')
    if mapper['geometry']['prg_bank_count_8k']!=16 or mapper['geometry']['prg_bank_bytes']!=BANK_BYTES: raise SystemExit('mapper geometry drift')

    # Every finite control edge with a physical ROM target must land on an accepted
    # instruction identity. This is the finite-graph half of the fixed-point proof.
    bad_targets=[]
    target_8000=[]
    for e in edges:
        tb=e['target_bank'].strip(); tp=e['target_pc'].strip()
        if not tb or not tp: continue
        b=int(tb); pc=int(tp,16)
        if b>=0:
            if 0x8000<=pc<=0x9fff: target_8000.append((b,pc,e['source_bank'],e['source_pc'],e['kind']))
            if (b,pc) not in bypc:
                bad_targets.append((b,pc,e['source_bank'],e['source_pc'],e['kind']))
    if bad_targets: raise SystemExit(f'V10 edge targets missing from accepted graph: {bad_targets[:4]}')
    if target_8000: raise SystemExit(f'V10 executable transfer reaches R6 window: {target_8000[:4]}')

    # Prove every accepted mapper-relevant write. Direct absolute writes are a
    # finite exact set. Only bank-select/data writes can change PRG execution map.
    direct_mapper=[]; indexed_mapper_candidates=[]; indy=[]
    for r in inst:
        if r['mnemonic'] not in WRITE_MNEMONICS: continue
        bs=bytes.fromhex(r['bytes']); mode=r['mode']
        if mode=='Abs' and len(bs)>=3 and u16(bs)>=0x8000:
            direct_mapper.append((int(r['physical_bank']),int(r['cpu_pc'],16),r['mnemonic'],u16(bs),r['bytes']))
        elif mode in ('AbsXW','AbsYW') and len(bs)>=3:
            base=u16(bs)
            if any(((base+i)&0xffff)>=0x8000 for i in range(256)):
                indexed_mapper_candidates.append((int(r['physical_bank']),int(r['cpu_pc'],16),r['mnemonic'],mode,base,r['bytes']))
        elif mode=='IndYW':
            indy.append((int(r['physical_bank']),int(r['cpu_pc'],16),r['bytes']))

    expected_direct=[
        (15,0xE038,'STY',0x8000,'8c0080'),
        (15,0xE03B,'STY',0xE000,'8c00e0'),
        (15,0xE051,'STA',0xA000,'8d00a0'),
        (15,0xE1EA,'STX',0x8000,'8e0080'),
        (15,0xE1ED,'STA',0x8001,'8d0180'),
        (15,0xE1F4,'STX',0x8000,'8e0080'),
        (15,0xE1F7,'STA',0x8001,'8d0180'),
    ]
    if direct_mapper!=expected_direct: raise SystemExit(f'direct mapper-write set drift: {direct_mapper}')
    if indexed_mapper_candidates!=[(15,0xEDF7,'STA','AbsYW',0xFFFF,'99ffff')]:
        raise SystemExit(f'indexed mapper candidate set drift: {indexed_mapper_candidates}')
    if indy!=[(15,0xE02F,'9100'),(15,0xEE8F,'9100')]: raise SystemExit(f'indirect write set drift: {indy}')

    # Reset proves Y=0 at E038: X starts 0, DEX -> FF, TXS, INX -> 0,
    # TXA/TAY -> Y=0. The RAM-clear loop preserves Y=0 on wrap before E038.
    exact_sequence(bypc,15,[
        (0xE012,'LDX','Imm','a200'),(0xE024,'DEX','Imp','ca'),(0xE025,'TXS','Imp','9a'),
        (0xE026,'INX','Imp','e8'),(0xE027,'LDA','Imm','a906'),(0xE029,'STA','Zero','8501'),
        (0xE02B,'TXA','Imp','8a'),(0xE02C,'TAY','Imp','a8'),(0xE02D,'STX','Zero','8600'),
        (0xE02F,'STA','IndYW','9100'),(0xE031,'INY','Imp','c8'),(0xE032,'BNE','Rel','d0fb'),
        (0xE034,'DEC','Zero','c601'),(0xE036,'BPL','Rel','10f7'),(0xE038,'STY','Abs','8c0080')])

    # E02F clears $0600 down through $0000; EE8F clears $0700-$077F.
    # Both indirect indexed write sites are therefore CPU-RAM-only.
    exact_sequence(bypc,15,[
        (0xEE86,'LDA','Imm','a907'),(0xEE88,'STA','Zero','8501'),(0xEE8A,'LDA','Imm','a900'),
        (0xEE8C,'STA','Zero','8500'),(0xEE8E,'TAY','Imp','a8'),(0xEE8F,'STA','IndYW','9100'),
        (0xEE91,'INY','Imp','c8'),(0xEE92,'CPY','Imm','c080'),(0xEE94,'BNE','Rel','d0f9')])

    # The sole indexed absolute candidate is syntactically $FFFF,Y, but EDC1
    # fixes A=6 before EDE2. EDE2 transfers A->X, starts Y=1, loops six times;
    # effective writes are $0000-$0005 after 16-bit wrap, never cartridge space.
    exact_sequence(bypc,15,[
        (0xEDC1,'LDA','Imm','a906'),(0xEDC3,'JSR','Abs','20e2ed'),
        (0xEDF2,'TAX','Imp','aa'),(0xEDF3,'LDY','Imm','a001'),(0xEDF5,'LDA','IndY','b110'),
        (0xEDF7,'STA','AbsYW','99ffff'),(0xEDFA,'INY','Imp','c8'),(0xEDFB,'DEX','Imp','ca'),
        (0xEDFC,'BNE','Rel','d0f7')])

    # E1E5 initializes R7..R0 with X=7..0. X never sets D6 while selecting
    # a register. E1F4 is the only later generic bank-data helper.
    exact_sequence(bypc,15,[
        (0xE1E5,'LDX','Imm','a207'),(0xE1E7,'LDA','AbsX','bde007'),
        (0xE1EA,'STX','Abs','8e0080'),(0xE1ED,'STA','Abs','8d0180'),
        (0xE1F0,'DEX','Imp','ca'),(0xE1F1,'BPL','Rel','10f4'),
        (0xE1F4,'STX','Abs','8e0080'),(0xE1F7,'STA','Abs','8d0180')])

    helper_calls=[]; r7_values=set(); x_values=set()
    bybank={}
    for r in inst: bybank.setdefault(int(r['physical_bank']),[]).append(r)
    for arows in bybank.values(): arows.sort(key=lambda r:int(r['cpu_pc'],16))
    for e in edges:
        if e['kind']!='call' or not e['target_pc'].strip(): continue
        target=int(e['target_pc'],16)
        if target not in (0xE1F4,0xFF6F): continue
        bank=int(e['source_bank']); pc=int(e['source_pc'],16); arows=bybank[bank]
        idx=next(i for i,r in enumerate(arows) if int(r['cpu_pc'],16)==pc)
        prior=arows[max(0,idx-12):idx]
        ldx=None; ldx_pos=None
        for j in range(len(prior)-1,-1,-1):
            q=prior[j]
            if q['mnemonic'] in ('LDX','TAX','TSX','INX','DEX'):
                ldx=q; ldx_pos=j; break
        if not ldx or ldx['mnemonic']!='LDX' or ldx['mode']!='Imm':
            raise SystemExit(f'helper call lacks finite immediate X proof {bank}:{pc:04X}')
        xv=bytes.fromhex(ldx['bytes'])[1]
        if xv not in (6,7): raise SystemExit(f'helper selects unexpected MMC3 register R{xv} at {bank}:{pc:04X}')
        x_values.add(xv); av=None
        if xv==7:
            for q in prior[ldx_pos+1:]:
                if q['mnemonic']=='LDA' and q['mode']=='Imm': av=bytes.fromhex(q['bytes'])[1]
                elif q['mnemonic'] in ('LDA','ADC','SBC','AND','ORA','EOR','PLA','TXA','TYA'):
                    av=None
            if av is None: raise SystemExit(f'R7 helper call lacks finite immediate A proof {bank}:{pc:04X}')
            if av not in (10,11,12,13): raise SystemExit(f'R7 value outside proved domain at {bank}:{pc:04X}: {av}')
            r7_values.add(av)
        helper_calls.append({'physical_bank':bank,'cpu_pc':f'0x{pc:04X}','target':f'0x{target:04X}','selected_register':xv,'r7_value':av})
    if r7_values!={10,11,12,13}: raise SystemExit(f'R7 fixed domain incomplete/drifted: {sorted(r7_values)}')
    if x_values!={6,7}: raise SystemExit(f'helper register domain drift: {sorted(x_values)}')
    if sorted(summary['executable_r7_values'])!=sorted(r7_values): raise SystemExit('V04/V10 R7 domain mismatch')

    # Byte ownership.  This is exhaustive over every physical PRG byte, not a
    # disassembler heuristic: accepted instruction spans + proved inline data +
    # two non-overlapping architectural/identity metadata regions; everything
    # else is excluded once the finite execution fixed point is established.
    owner=['excluded_immutable_data']*PRG_BYTES
    detail=['outside closed executable fixed point']*PRG_BYTES
    def claim(off,n,kind,why):
        for i in range(off,off+n):
            if not 0<=i<PRG_BYTES: raise SystemExit('ownership range outside PRG')
            if owner[i] != 'excluded_immutable_data': raise SystemExit(f'ownership overlap at PRG+0x{i:05X}')
            owner[i]=kind; detail[i]=why
    for r in inst:
        bank=int(r['physical_bank']); pc=int(r['cpu_pc'],16); n=int(r['size']); off=bank*BANK_BYTES+(pc&0x1fff)
        claim(off,n,'code','accepted V04 instruction bytes')
    for r in inline:
        bank=int(r['physical_bank']); pc=int(r['start_pc'],16); n=int(r['byte_count']); off=bank*BANK_BYTES+(pc&0x1fff)
        claim(off,n,'inline_data',r['kind'])
    metadata=[(15,0x1FE0,16,'embedded title identity signature'),(15,0x1FFA,6,'architectural interrupt vectors')]
    for bank,boff,n,why in metadata: claim(bank*BANK_BYTES+boff,n,'metadata',why)

    counts={k:owner.count(k) for k in ('code','inline_data','metadata','excluded_immutable_data')}
    expected={'code':35423,'inline_data':244,'metadata':22,'excluded_immutable_data':95383}
    if counts!=expected or sum(counts.values())!=PRG_BYTES: raise SystemExit(f'V10 byte ownership count drift: {counts}')

    # Compress the exhaustive ownership map into deterministic contiguous ranges.
    range_rows=[]; s=0
    while s<PRG_BYTES:
        k=owner[s]; why=detail[s]; bank=s//BANK_BYTES; e=s+1
        bank_end=(bank+1)*BANK_BYTES
        while e<bank_end and owner[e]==k and detail[e]==why: e+=1
        range_rows.append({'physical_bank':bank,'start_bank_offset':f'0x{s%BANK_BYTES:04X}',
                           'end_bank_offset_inclusive':f'0x{(e-1)%BANK_BYTES:04X}',
                           'length':e-s,'class':k,'reason':why})
        s=e
    with (out/'romwide-byte-ownership.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(range_rows[0])); w.writeheader(); w.writerows(range_rows)

    mapper_proof={
      'schema':1,'milestone':'V10','construction_authority':'exact ROM + sealed V04 graph/proofs + MMC3 architecture only',
      'rom_sha256':EXPECTED_ROM_SHA256,'prg_sha256':EXPECTED_PRG_SHA256,
      'bank_select_d6_domain':[0],
      'generic_helper_selected_registers':sorted(x_values),
      'r7_executable_values':sorted(r7_values),
      'r6_window_executable_targets':0,
      'prg_mode':'D6=0 for every accepted execution-relevant bank-select write',
      'cpu_windows':{'0x8000-0x9FFF':'R6; formally non-executable','0xA000-0xBFFF':'R7 in {10,11,12,13}',
                     '0xC000-0xDFFF':'fixed physical bank 14','0xE000-0xFFFF':'fixed physical bank 15'},
      'executable_physical_banks':[10,11,12,13,14,15],
      'formally_excluded_physical_banks':[0,1,2,3,4,5,6,7,8,9],
      'direct_mapper_writes':[{'physical_bank':b,'cpu_pc':f'0x{pc:04X}','mnemonic':mn,'address':f'0x{addr:04X}','bytes':hx}
                              for b,pc,mn,addr,hx in direct_mapper],
      'generic_helper_calls':helper_calls,
      'indirect_indexed_writes':[{'physical_bank':b,'cpu_pc':f'0x{pc:04X}','proof':'CPU RAM only'} for b,pc,_ in indy],
      'indexed_absolute_candidate':{'physical_bank':15,'cpu_pc':'0xEDF7','syntactic_base':'0xFFFF','proved_y_values':[1,2,3,4,5,6],
                                    'effective_addresses':['0x0000','0x0001','0x0002','0x0003','0x0004','0x0005']},
      'result':'PASS'
    }
    write_json(out/'mapper-execution-domain.json',mapper_proof)

    fixed={
      'schema':1,'milestone':'V10','result':'PASS','rom_wide_fixed_point':True,
      'construction_method':'offline static exact-ROM fixed point; no gameplay/emulator-observed targets',
      'rom_sha256':EXPECTED_ROM_SHA256,'payload_sha256':EXPECTED_PAYLOAD_SHA256,'prg_sha256':EXPECTED_PRG_SHA256,
      'prg_bytes':PRG_BYTES,'accepted_instruction_contexts':len(inst),'accepted_instruction_bytes':counts['code'],
      'inline_data_bytes':counts['inline_data'],'metadata_bytes':counts['metadata'],
      'statically_excluded_immutable_data_bytes':counts['excluded_immutable_data'],'unresolved_prg_bytes':0,
      'unresolved_current_graph_edges':0,'executable_ram_epochs':0,'self_modifying_code_epochs':0,
      'edge_records':len(edges),'executable_physical_banks':[10,11,12,13,14,15],
      'formally_excluded_physical_banks':[0,1,2,3,4,5,6,7,8,9],
      'fixed_point_argument':[
        'RESET/NMI/IRQ and all reviewed roots are contained in the accepted finite V04 graph.',
        'Every finite V04 control edge lands on an accepted instruction and zero current-graph edges are unresolved.',
        'All indirect, stack-dispatch, return-continuation and executable-RAM cases are finite and closed by V04 authority.',
        'V10 proves all reachable MMC3 bank-select writes keep PRG mode D6=0; R7 execution is exactly banks 10-13.',
        'There are zero executable transfers into the R6 $8000-$9FFF window, so R6 values cannot create code authority.',
        'Fixed banks 14/15 plus R7 banks 10-13 therefore exhaust every executable PRG mapping reachable from roots.',
        'With no unresolved edge, executable RAM epoch, or additional mapper execution domain, no unowned PRG byte can become a new executable root.'
      ]
    }
    write_json(out/'fixed-point-summary.json',fixed)

    # Inputs and outputs are individually pinned so unchanged heavy proof can be
    # hash-gated instead of repeatedly rebuilt or revalidated by compilation.
    input_files=[
      'generated/v04/closure-summary.json','generated/v04/instructions.csv','generated/v04/edges.csv',
      'generated/v04/unresolved-edges.csv','generated/v04/inline-data.csv','generated/v04/indirect-targets.csv',
      'generated/v04/return-continuations.csv','generated/v04/executable-ram-epochs.json',
      'config/v04-static-proofs.json','config/mapper-contract.json','generated/v03/data-evidence.csv']
    outputs=['romwide-byte-ownership.csv','mapper-execution-domain.json','fixed-point-summary.json']
    manifest={'schema':1,'milestone':'V10','rom_sha256':EXPECTED_ROM_SHA256,
              'inputs':{p:sha_file(root/p) for p in input_files},
              'outputs':{p:sha_file(out/p) for p in outputs}}
    write_json(out/'generated-manifest.json',manifest)
    print(json.dumps({'result':'PASS','counts':counts,'contexts':len(inst),'edges':len(edges),
                      'r7_values':sorted(r7_values),'helper_calls':len(helper_calls),'ranges':len(range_rows)},sort_keys=True))

if __name__=='__main__': main()
