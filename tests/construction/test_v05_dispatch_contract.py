#!/usr/bin/env python3
from __future__ import annotations
import re, shutil, subprocess, tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
GEN=ROOT/'generated/v05'

def main()->int:
    header=(GEN/'ai2_v05_runtime_api.h').read_text()
    names=re.findall(r'^int (ai2_v05_op_[A-Za-z0-9_]+)\(Ai2V05Runtime \*rt, uint16_t source_pc, uint16_t operand, Ai2V05SemanticResult \*result\);$',header,re.M)
    if len(names)!=99: raise SystemExit(f'expected 99 fixed semantic symbols, got {len(names)}')
    with tempfile.TemporaryDirectory(prefix='ai2-v05-dispatch-') as td:
        td=Path(td); stub=td/'stub.c'; exe=td/'test'
        parts=['''#include <stdint.h>\n#include <stdio.h>\n#include "ai2_v05_generated.h"\nstruct Ai2V05Runtime { uint8_t origin_bank; uint16_t origin_pc; uint8_t has_origin; };\nint ai2_v05_runtime_map_prg_identity(Ai2V05Runtime *rt, uint16_t pc, uint8_t *bank) { (void)rt; if(pc>=0xE000u)*bank=15u; else if(pc>=0xC000u)*bank=14u; else if(pc>=0xA000u)*bank=10u; else return 1; return 0; }\nint ai2_v05_runtime_note_stack_dispatch_origin(Ai2V05Runtime *rt,uint8_t bank,uint16_t pc){rt->origin_bank=bank;rt->origin_pc=pc;rt->has_origin=1;return 0;}\nint ai2_v05_runtime_get_stack_dispatch_origin(Ai2V05Runtime *rt,uint8_t *bank,uint16_t *pc){if(!rt->has_origin)return 1;*bank=rt->origin_bank;*pc=rt->origin_pc;return 0;}\nvoid ai2_v05_runtime_clear_stack_dispatch_origin(Ai2V05Runtime *rt){rt->has_origin=0;}\n''']
        for name in names:
            if name.startswith('ai2_v05_op_6C_'):
                body='if(result){result->branch_taken=0u; result->dynamic_pc=(source_pc==0xEDB6u)?0xA7FDu:0xAC32u;} (void)rt; (void)operand; return 0;'
            elif name.startswith('ai2_v05_op_60_'):
                body='if(result){result->branch_taken=0u; result->dynamic_pc=0xA7C4u;} (void)rt; (void)source_pc; (void)operand; return 0;'
            elif name.startswith('ai2_v05_op_40_'):
                body='if(result){result->branch_taken=0u; result->dynamic_pc=0xE153u;} (void)rt; (void)source_pc; (void)operand; return 0;'
            elif name.startswith('ai2_v05_op_D0_'):
                body='if(result){result->branch_taken=(source_pc==0xA5FBu); result->dynamic_pc=0u;} (void)rt; (void)operand; return 0;'
            else:
                body='if(result){result->branch_taken=0u; result->dynamic_pc=0u;} (void)rt; (void)source_pc; (void)operand; return 0;'
            parts.append(f'int {name}(Ai2V05Runtime *rt,uint16_t source_pc,uint16_t operand,Ai2V05SemanticResult *result){{{body}}}\n')
        parts.append(r'''
static int require(int cond, const char *what){ if(!cond){fprintf(stderr,"FAIL %s\n",what);return 1;} return 0; }
int main(void){
  Ai2V05Runtime rt={0}; Ai2V05Identity next={0}; Ai2V05StepResult r; int fail=0;
  fail|=require(ai2_v05_generated_identity_exists(10u,0xA006u)==1,"known identity");
  fail|=require(ai2_v05_generated_identity_exists(0u,0x8000u)==0,"empty bank identity");
  fail|=require(ai2_v05_validate_indirect(10u,0xABFBu,10u,0xAC32u)==1,"indirect accept");
  fail|=require(ai2_v05_validate_indirect(10u,0xABFBu,10u,0xAC33u)==0,"indirect reject");
  fail|=require(ai2_v05_validate_stack_dispatch(10u,0xA7ECu,10u,0xA7FDu)==1,"stack accept");
  fail|=require(ai2_v05_validate_stack_dispatch(10u,0xA7EDu,10u,0xA7FDu)==0,"stack origin reject");
  fail|=require(ai2_v05_validate_return(10u,0xA7C4u)==1,"return accept");
  fail|=require(ai2_v05_validate_return(0u,0x8000u)==0,"return reject");
  r=ai2_v05_dispatch(&rt,(Ai2V05Identity){255u,0x8000u},&next); fail|=require(r==AI2_V05_STEP_MISSING_BANK,"missing bank trap");
  r=ai2_v05_dispatch(&rt,(Ai2V05Identity){0u,0x8000u},&next); fail|=require(r==AI2_V05_STEP_MISSING_IDENTITY,"missing pc trap");
  r=ai2_v05_dispatch(&rt,(Ai2V05Identity){10u,0xA006u},&next); fail|=require(r==AI2_V05_STEP_OK && next.bank==10u && next.pc==0xA7EAu,"fixed JMP");
  r=ai2_v05_dispatch(&rt,(Ai2V05Identity){10u,0xA7A6u},&next); fail|=require(r==AI2_V05_STEP_OK && next.bank==10u && next.pc==0xA7A8u,"fixed fallthrough");
  r=ai2_v05_dispatch(&rt,(Ai2V05Identity){11u,0xA5FBu},&next); fail|=require(r==AI2_V05_STEP_OK && next.bank==11u && next.pc==0xA601u,"taken branch");
  r=ai2_v05_dispatch(&rt,(Ai2V05Identity){10u,0xABFBu},&next); fail|=require(r==AI2_V05_STEP_OK && next.bank==10u && next.pc==0xAC32u,"finite indirect dispatch");
  r=ai2_v05_dispatch(&rt,(Ai2V05Identity){10u,0xA7ECu},&next); fail|=require(r==AI2_V05_STEP_OK && next.bank==15u && next.pc==0xFF84u && rt.has_origin && rt.origin_bank==10u && rt.origin_pc==0xA7ECu,"automatic stack origin");
  r=ai2_v05_dispatch(&rt,(Ai2V05Identity){15u,0xEDB6u},&next); fail|=require(r==AI2_V05_STEP_OK && next.bank==10u && next.pc==0xA7FDu,"stack finite dispatch");
  r=ai2_v05_dispatch(&rt,(Ai2V05Identity){10u,0xA7C4u},&next); fail|=require(r==AI2_V05_STEP_OK && next.bank==10u && next.pc==0xA7C4u,"finite return domain");
  r=ai2_v05_dispatch(&rt,(Ai2V05Identity){15u,0xE152u},&next); fail|=require(r==AI2_V05_STEP_OK && next.bank==15u && next.pc==0xE153u,"RTI identity flow");
  r=ai2_v05_execute_compact(&rt,NULL,0u,10u,0xA006u,&next); fail|=require(r==AI2_V05_STEP_SEMANTIC_TRAP,"null compact rows");
  r=ai2_v05_execute_compact(&rt,(const Ai2V05CompactInstruction*)1,0u,10u,0xA006u,&next); fail|=require(r==AI2_V05_STEP_MISSING_IDENTITY,"empty compact rows");
  if(fail) return 1;
  puts("V05_DISPATCH_CONTRACT PASS checks=20");
  return 0;
}
''')
        stub.write_text(''.join(parts),newline='\n')
        cc=shutil.which('gcc') or shutil.which('clang')
        if not cc: raise SystemExit('no C compiler')
        cfiles=[str(p) for p in sorted(GEN.glob('*.c'))]
        proc=subprocess.run([cc,'-std=c11','-O0','-Wall','-Wextra','-Werror',f'-I{GEN}',*cfiles,str(stub),'-o',str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if proc.returncode: raise SystemExit('compile/link failed\n'+proc.stdout[:5000])
        proc=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        print(proc.stdout,end='')
        return proc.returncode
if __name__=='__main__': raise SystemExit(main())
