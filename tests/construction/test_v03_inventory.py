#!/usr/bin/env python3
from __future__ import annotations
import argparse,csv,hashlib,json,re,subprocess,sys,tempfile
from pathlib import Path


def h(data: bytes) -> str: return hashlib.sha256(data).hexdigest()
def loadj(p: Path): return json.loads(p.read_text(encoding='utf-8'))
def rows(p: Path):
    with p.open(newline='',encoding='utf-8') as f: return list(csv.DictReader(f))
def x(s: str) -> int: return int(s,16)

BRANCHES={"BPL","BMI","BVC","BVS","BCC","BCS","BNE","BEQ"}
TERMINATORS=BRANCHES|{"RTS","RTI","JMP","JSR","BRK","STP"}

def main()->int:
    ap=argparse.ArgumentParser()
    ap.add_argument('project',type=Path)
    ap.add_argument('rom',type=Path)
    args=ap.parse_args()
    p=args.project.resolve(); rom_path=args.rom.resolve(); g=p/'generated/v03'
    checks=0
    def ck(cond,msg):
        nonlocal checks
        checks+=1
        if not cond: raise AssertionError(msg)

    profile=loadj(p/'config/target-profile.json'); mapper=loadj(p/'config/mapper-contract.json'); opmeta=loadj(p/'config/2a03-opcodes.json')
    pins=loadj(p/'config/source-pins.json')
    ck(pins['project_tools']['disassembler_entry']['version']=='0.3.0-v03','disassembler version pin')
    ck(pins['project_tools']['disassembler_entry']['sha256']==h((p/'tools/disassemble_program_map.py').read_bytes()),'disassembler hash pin')
    ck(pins['v03_disassembly_reference']['opcode_metadata_sha256']==h((p/'config/2a03-opcodes.json').read_bytes()),'opcode metadata hash pin')
    rom=rom_path.read_bytes(); prg=rom[16:16+131072]
    ck(h(rom)==profile['rom']['file_sha256'],'exact ROM hash')
    ck(h(rom[16:])==profile['rom']['payload_sha256'],'payload hash')
    ck(len(opmeta['entries'])==256,'opcode count')

    # Pinned Mesen opcode metadata equivalence.
    workspace=p.parents[1]
    mesen=workspace/'Reference/Oracle-Source-Unpacked/Linux/MesenCE-2.2.1-NES-Oracle-Linux-Source/Core/NES/Debugger/NesDisUtils.cpp'
    ck(mesen.is_file(),'pinned Mesen NesDisUtils.cpp exists')
    src=mesen.read_text(encoding='utf-8')
    ck(h(mesen.read_bytes())==opmeta['source_sha256'],'opcode source hash pin')
    names=re.findall(r'"([A-Z0-9]+)"',re.search(r'static constexpr const char\* _opName\[256\] = \{(.*?)\n\};',src,re.S).group(1))
    modes=re.findall(r'M::([A-Za-z0-9]+)',re.search(r'static constexpr NesAddrMode _opMode\[\] = \{(.*?)\n\};',src,re.S).group(1))
    flags=[v=='true' for v in re.findall(r'\b(true|false)\b',re.search(r'static constexpr bool _isUnofficial\[256\] = \{(.*?)\n\};',src,re.S).group(1))]
    ck(len(names)==len(modes)==len(flags)==256,'parsed source opcode tables')
    for i,e in enumerate(opmeta['entries']):
        ck(e['opcode']==i and e['mnemonic']==names[i] and e['mode']==modes[i] and e['unofficial']==flags[i],f'opcode metadata {i:02X}')

    summary=loadj(g/'summary.json'); banks=loadj(g/'prg-bank-inventory.json')['banks']
    ck(summary['physical_prg_banks']==16,'16 banks')
    ck(summary['prg_bytes_covered_exactly_once_by_primary_classification']==131072,'all PRG bytes covered')
    ck(summary['padding_classification_count']==0,'no padding labels')
    ck(summary['decode_conflict_count']==0,'no decode conflicts')
    ck(summary['overlap_conflict_count']==0,'no overlaps')
    ck(summary['v04_not_performed'] is True,'V04 not performed')
    ck(summary['production_lowering_performed'] is False,'no lowering')
    ck(summary['gameplay_used'] is False,'no gameplay')
    ck(len(banks)==16,'bank inventory count')
    for b,row in enumerate(banks):
        ck(row['physical_prg_bank']==b,'bank order')
        ck(row['size_bytes']==8192,'bank size')
        ck(row['sha256']==h(prg[b*8192:(b+1)*8192]),f'bank {b} hash')
        ck(row['padding_bytes']==0,f'bank {b} padding zero')
        ck(row['code_bytes']+row['vector_bytes']+row['text_bytes']+row['unclassified_bytes']==8192,f'bank {b} classified total')

    # Classification ranges exactly partition each physical bank with no gaps/overlap.
    cr=rows(g/'classification-ranges.csv')
    bybank={i:[] for i in range(16)}
    for r in cr:
        ck(r['primary_class'] in {'unclassified','code','vector','text'},'allowed primary class')
        ck(r['primary_class']!='padding','never padding')
        bybank[int(r['physical_prg_bank'])].append(r)
    total=0
    for b in range(16):
        pos=0
        for r in bybank[b]:
            s=x(r['start_bank_offset']); e=x(r['end_bank_offset_inclusive']); ln=int(r['length'])
            ck(s==pos,f'bank {b} contiguous at {pos:04X}')
            ck(e>=s and ln==e-s+1,'range length')
            pos=e+1; total+=ln
        ck(pos==8192,f'bank {b} full range')
    ck(total==131072,'range grand total')

    # Every accepted instruction is reproducibly tied to exact physical ROM bytes and opcode metadata.
    il=rows(g/'instruction-ledger.csv'); ck(len(il)==summary['accepted_instruction_count']==10,'instruction count')
    meta={e['opcode']:e for e in opmeta['entries']}
    ids=set()
    for r in il:
        b=int(r['physical_prg_bank']); off=x(r['bank_offset']); pc=x(r['cpu_pc']); ln=int(r['length']); data=prg[b*8192+off:b*8192+off+ln]
        ck((b,off) not in ids,'unique instruction identity'); ids.add((b,off))
        ck(h(data)==r['instruction_sha256'],'instruction byte hash')
        e=meta[data[0]]
        ck(e['size']==ln and e['mnemonic']==r['mnemonic'] and e['mode']==r['addressing_mode'],'instruction decode metadata')
        ck((r['unofficial_opcode']=='yes')==e['unofficial'],'unofficial flag')
        ck(b==15 and 0xE000<=pc<=0xFFFF and off==(pc&0x1fff),'fixed bank physical identity')

    # Independently reconstruct V03 straight-line root policy and verify it stops at first control transfer.
    roots=rows(g/'roots.csv'); rootmap={r['root']:r for r in roots}; ck(set(rootmap)=={'reset','nmi','irq'},'three vector roots')
    ledger_by={(int(r['physical_prg_bank']),x(r['bank_offset'])):r for r in il}
    for root in ('reset','nmi','irq'):
        pc=int(profile['vectors'][root],16); count=0
        while True:
            off=15*8192+(pc&0x1fff); e=meta[prg[off]]; ident=(15,pc&0x1fff)
            ck(ident in ledger_by,f'{root} expected instruction {pc:04X}')
            ck(root in ledger_by[ident]['root_origins'].split(';'),f'{root} origin tag')
            count+=1
            if e['mnemonic'] in TERMINATORS: break
            pc=(pc+e['size'])&0xffff
        ck(count==int(rootmap[root]['accepted_straight_line_instruction_count']),f'{root} root count')

    flow=rows(g/'control-flow-seeds.csv'); ck(len(flow)==4,'four V04 control-flow seeds')
    for r in flow: ck(r['v04_status']=='pending','V04 seed pending')
    ck(len(rows(g/'decode-conflicts.csv'))==0,'empty decode conflict ledger')
    ck(len(rows(g/'overlap-conflicts.csv'))==0,'empty overlap conflict ledger')

    ev=rows(g/'data-evidence.csv')
    ck(sum(r['category']=='table' for r in ev)==1,'vector table classified')
    ck(sum(r['category']=='pointer' for r in ev)==3,'three vector pointers classified')
    ck(sum(r['category']=='text' for r in ev)==1,'text signature classified')
    ck(sum(r['category']=='graphics_reference' and int(r['length'])>0 for r in ev)==0,'no guessed graphics references')

    # Generated artifacts intentionally contain no ROM files.
    ck(not any(q.suffix.lower()=='.nes' for q in p.rglob('*') if q.is_file()),'no ROM inside project')

    # Clean reproducibility: two independent V03 generations must be byte-identical.
    tool=p/'tools/disassemble_program_map.py'
    with tempfile.TemporaryDirectory() as td:
        a=Path(td)/'a'; b=Path(td)/'b'
        for out in (a,b):
            cp=subprocess.run([sys.executable,str(tool),str(rom_path),'--out',str(out),'--project-root',str(p)],capture_output=True,text=True)
            ck(cp.returncode==0,cp.stderr or cp.stdout)
        an={q.name:h(q.read_bytes()) for q in a.iterdir() if q.is_file()}; bn={q.name:h(q.read_bytes()) for q in b.iterdir() if q.is_file()}
        ck(an==bn,'two clean generations byte-identical')
        ck(an=={q.name:h(q.read_bytes()) for q in g.iterdir() if q.is_file()},'checked-in generated tree matches clean generation')

        bad=Path(td)/'altered.nes'; altered=bytearray(rom); altered[0x1234]^=1; bad.write_bytes(altered)
        cp=subprocess.run([sys.executable,str(tool),str(bad),'--out',str(Path(td)/'bad'),'--project-root',str(p)],capture_output=True,text=True)
        ck(cp.returncode!=0 and 'SHA-256 mismatch' in (cp.stderr+cp.stdout),'altered ROM rejected')

    print(f'PASS: V03 inventory construction tests; checks={checks}; instructions={len(il)}; PRG bytes={total}; banks=16')
    return 0

if __name__=='__main__':
    raise SystemExit(main())
