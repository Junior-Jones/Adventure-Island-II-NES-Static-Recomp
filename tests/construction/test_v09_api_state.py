#!/usr/bin/env python3
import argparse,concurrent.futures,hashlib,json,os,shutil,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
ap=argparse.ArgumentParser();ap.add_argument('--rom',required=True);ap.add_argument('--mode',choices=['gcc','clang','sanitize'],default='gcc');ap.add_argument('--frontend',action='store_true');ap.add_argument('--json-out');a=ap.parse_args()
compiler='clang' if a.mode in ('clang','sanitize') else 'gcc'
if not shutil.which(compiler): raise SystemExit(f'{compiler} not found')
incs=[f'-I{root}',f'-I{root/"include"}',f'-I{root/"src"}',f'-I{root/"src/runtime"}',f'-I{root/"src/mapper"}',f'-I{root/"src/ppu"}',f'-I{root/"src/apu"}',f'-I{root/"src/api"}',f'-I{root/"generated/v05"}',f'-I{root/"generated/v09"}']
common=['-D_POSIX_C_SOURCE=200809L','-std=c11','-Wall','-Wextra','-Werror','-pedantic',*incs]
if a.mode=='sanitize': cflags=['-O0','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']; lflags=['-fsanitize=address,undefined']
else: cflags=['-O2'];lflags=[]
core=[root/'src/api/ai2_v09_core.c',root/'src/api/ai2_sha256.c',root/'src/apu/ai2_v08_apu.c',root/'src/ppu/ai2_v07_ppu.c',root/'src/runtime/ai2_v06_runtime.c',root/'src/runtime/ai2_v06_semantics.c',root/'src/mapper/ai2_mmc3_mapper.c',root/'generated/v05/bank_dispatch.c',root/'generated/v05/indirect_domains.c',root/'generated/v05/return_domains.c']+[root/f'generated/v05/bank_{i:02d}.c' for i in range(16)]
result={'schema':1,'mode':a.mode,'compiler':compiler,'frontend_equivalence':False,'functional_pass':False}
with tempfile.TemporaryDirectory(prefix='ai2-v09-gate-') as td:
    td=Path(td);obj=td/'obj';obj.mkdir();state=td/'state';state.mkdir()
    def compile_one(src):
        name=hashlib.sha256(str(src).encode()).hexdigest()[:16]+'.o';out=obj/name
        subprocess.run([compiler,*common,*cflags,'-c',str(src),'-o',str(out)],check=True)
        return out
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(8,os.cpu_count() or 2)) as ex: objects=list(ex.map(compile_one,core))
    test_obj=compile_one(root/'tests/construction/test_v09_api_state.c');exe=td/'test-v09'
    subprocess.run([compiler,str(test_obj),*map(str,objects),*lflags,'-o',str(exe)],check=True)
    env=os.environ.copy()
    if a.mode=='sanitize': env.update(ASAN_OPTIONS='detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1')
    p=subprocess.run([str(exe),a.rom,str(state)],check=True,text=True,capture_output=True,env=env)
    print(p.stdout.strip());result['functional_pass']=True;result['functional_stdout']=p.stdout.strip()
    if a.frontend:
        common_obj=compile_one(root/'frontend/common/ai2_frontend_session.c')
        outs=[]
        for label,src in [('headless',root/'frontend/headless/ai2_headless.c'),('headed-boundary',root/'frontend/headed/ai2_headed_boundary.c')]:
            client=compile_one(src);app=td/f'ai2-{label}';subprocess.run([compiler,str(client),str(common_obj),*map(str,objects),*lflags,'-o',str(app)],check=True)
            q=subprocess.run([str(app),a.rom,'3'],check=True,text=True,capture_output=True,env=env).stdout.strip();print(q);outs.append(q.split(' ',1)[1])
        if outs[0]!=outs[1]: raise SystemExit('headed/headless state differs')
        print('V09 headed/headless shared-frame equivalence: PASS');result['frontend_equivalence']=True
if a.json_out: Path(a.json_out).write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
