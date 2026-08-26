#!/usr/bin/env python3
import json, re, subprocess, sys, tempfile
from pathlib import Path

root=Path(__file__).resolve().parents[2]
contracts=json.loads((root/'generated/v05/opcode-form-contracts.json').read_text())
forms=contracts['forms']
assert contracts['form_count']==99==len(forms)
assert all(not f['runtime_opcode_fetch'] and not f['runtime_opcode_decode'] for f in forms)

api=(root/'generated/v05/ai2_v05_runtime_api.h').read_text()
sem=(root/'src/runtime/ai2_v06_semantics.c').read_text()
api_syms=set(re.findall(r'int (ai2_v05_op_[A-Za-z0-9_]+)\(',api))
# Macros obscure definitions in raw source, so every authority symbol must at least occur in semantic source.
contract_syms={f['symbol'] for f in forms}
assert api_syms==contract_syms, (len(api_syms),len(contract_syms), sorted(api_syms^contract_syms))
missing=[s for s in sorted(contract_syms) if s not in sem]
assert not missing, missing

cycles={}
# Standard NMOS 6502/NES no-page-cross baseline cycles for the exact forms used by V04.
for f in forms:
    op=int(f['opcode_form'],16); mode=f['mode']; mn=f['mnemonic']
    if mode in ('Imm','Rel','Imp','Acc'): c=2
    elif mode=='Zero': c=5 if mn in ('ASL','ROL','LSR','ROR','DEC','INC') else 3
    elif mode=='ZeroX': c=6 if mn in ('DEC','INC') else 4
    elif mode=='Abs': c=6 if mn in ('DEC','INC') else 4
    elif mode=='AbsX': c=4
    elif mode=='AbsXW': c=7 if mn in ('DEC','INC') else 5
    elif mode=='AbsY': c=4
    elif mode=='AbsYW': c=5
    elif mode=='IndY': c=5
    elif mode=='IndYW': c=6
    elif mode=='Ind': c=5
    else: raise AssertionError((f,mode))
    if mn in ('PHP','PHA'): c=3
    if mn in ('PLP','PLA'): c=4
    if mn in ('RTS','RTI'): c=6
    if mn=='JSR': c=6
    if mn=='JMP' and mode=='Abs': c=3
    cycles[op]=c

lines=['''#include "src/runtime/ai2_v06_runtime.h"\n#include <stdio.h>\n#include <string.h>\nstatic unsigned char prg[AI2_PRG_ROM_BYTES], chr[AI2_CHR_ROM_BYTES];\nstatic unsigned checks, fails;\n#define CK(x) do { checks++; if(!(x)){ fprintf(stderr,"FAIL line %d: %s\\n",__LINE__,#x); fails++; } } while(0)\nstatic void prep(Ai2V05Runtime *rt, unsigned op) {\n ai2_v06_init(rt,prg,sizeof(prg),chr,sizeof(chr),0);\n rt->a=0x31; rt->x=0; rt->y=0; rt->p=AI2_V06_FLAG_U;\n rt->ram[0x10]=0x22; rt->ram[0x20]=0x30; rt->ram[0x21]=0; rt->ram[0x30]=0x44;\n rt->ram[0x02ff]=0x34; rt->ram[0x0200]=0x12;\n if(op==0x10) rt->p|=AI2_V06_FLAG_N; /* BPL not taken */\n if(op==0x30) rt->p&=(unsigned char)~AI2_V06_FLAG_N;\n if(op==0x50) rt->p|=AI2_V06_FLAG_V;\n if(op==0x70) rt->p&=(unsigned char)~AI2_V06_FLAG_V;\n if(op==0x90) rt->p|=AI2_V06_FLAG_C;\n if(op==0xB0) rt->p&=(unsigned char)~AI2_V06_FLAG_C;\n if(op==0xD0) rt->p|=AI2_V06_FLAG_Z;\n if(op==0xF0) rt->p&=(unsigned char)~AI2_V06_FLAG_Z;\n if(op==0x28 || op==0x68) { rt->s=0xfc; rt->ram[0x01fd]=AI2_V06_FLAG_U; }\n if(op==0x60) { rt->s=0xfb; rt->ram[0x01fc]=0x02; rt->ram[0x01fd]=0x02; }\n if(op==0x40) { rt->s=0xfa; rt->ram[0x01fb]=AI2_V06_FLAG_U; rt->ram[0x01fc]=0x34; rt->ram[0x01fd]=0x12; }\n}\nint main(void) { Ai2V05Runtime rt; Ai2V05SemanticResult r; unsigned long long before;\n''']
for f in forms:
    op=int(f['opcode_form'],16); sym=f['symbol']; mode=f['mode']
    operand='0x0010u'
    if mode=='IndY' or mode=='IndYW': operand='0x0020u'
    if mode=='Ind': operand='0x02ffu'
    if f['mnemonic']=='JSR': operand='0x1234u'
    lines.append(f' prep(&rt,0x{op:02X}u); before=rt.cpu_cycles; CK({sym}(&rt,0x0400u,{operand},&r)==0); CK(!ai2_v06_has_trap(&rt)); CK(rt.cpu_cycles-before=={cycles[op]}u);\n')
lines.append(' printf("V06 all fixed opcode forms: %s (%u checks, 99 forms)\\n",fails?"FAIL":"PASS",checks); return fails?1:0; }\n')
source=''.join(lines)

sources=[str(root/'src/mapper/ai2_mmc3_mapper.c'),str(root/'src/runtime/ai2_v06_runtime.c'),str(root/'src/runtime/ai2_v06_semantics.c')]
sources += [str(p) for p in sorted((root/'generated/v05').glob('*.c'))]
include=['-I'+str(root),'-I'+str(root/'src'),'-I'+str(root/'src/mapper'),'-I'+str(root/'generated/v05')]
compilers=[c for c in ('gcc','clang') if subprocess.run(['sh','-c',f'command -v {c} >/dev/null 2>&1']).returncode==0]
assert compilers, 'no C compiler'
with tempfile.TemporaryDirectory(prefix='ai2-v06-forms-') as td:
    td=Path(td); cfile=td/'all_forms.c'; cfile.write_text(source)
    for cc in compilers:
        exe=td/f'all_forms-{cc}'
        cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-pedantic',*include,str(cfile),*sources,'-o',str(exe)]
        subprocess.run(cmd,check=True)
        subprocess.run([str(exe)],check=True)
print(f'V06 all-forms source/compile gate: PASS (99 forms; compilers={",".join(compilers)})')
