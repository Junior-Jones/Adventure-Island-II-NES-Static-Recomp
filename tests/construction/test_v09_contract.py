#!/usr/bin/env python3
import hashlib,json,re,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
fail=[]
def ck(x,msg):
    if not x: fail.append(msg)
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
# Deterministic identity regeneration must be a no-op.
hdr=root/'generated/v09/ai2_v09_identity.h'; cfg=root/'config/v09-core-contract.json'
before=(sha(hdr),sha(cfg))
p=subprocess.run([sys.executable,str(root/'tools/generate_v09_core_identity.py')],text=True,capture_output=True)
ck(p.returncode==0,'identity generator failed')
after=(sha(hdr),sha(cfg));ck(before==after,'V09 identity regeneration changed output')
contract=json.loads(cfg.read_text())
ck(contract.get('schema')==1 and contract.get('milestone')=='V09','contract identity')
ck(contract.get('rom_sha256')=='10dd53fd62c95e77856b09c693b3bdd14b62a89988902eea111257cafe9b8acc','ROM pin')
ck(contract.get('starter_nes4_zip_sha256')=='a895ce1d21ce41d10ef9393b47bdb2f315db42e1a6af2477f9b79a03a1923703','Starter pin')
for rel,h in contract['source_sha256'].items(): ck(sha(root/rel)==h,f'core source hash drift: {rel}')
identity=p.stdout.strip().splitlines()[-1] if p.stdout.strip() else ''
ck(identity==contract['core_compatibility_sha256'],'identity output mismatch')
ht=hdr.read_text();ck(identity in ht,'identity header mismatch')
public=(root/'include/ai2_static_core.h').read_text()
for banned in ('Ai2V05Runtime','Ai2V07Ppu','Ai2V08Apu','Ai2Mmc3State','ai2_v06_','ai2_v07_','ai2_v08_'): ck(banned not in public,f'private symbol leaked to public header: {banned}')
ck(re.search(r'typedef struct Ai2StaticCore Ai2StaticCore;',public) is not None,'opaque forward declaration missing')
core=(root/'src/api/ai2_v09_core.c').read_text()
for token in ('core_safe_boundary','AI2_SNAPSHOT_PAYLOAD_HASH_MISMATCH','atomic_replace','AI2_RUNTIME_EVENT_FRONTIER','AI2_CORE_STATUS_REENTRANT_CALL','AI2_V09_CORE_COMPAT_DIGEST','AI2_V09_ROM_DIGEST'): ck(token in core,f'core contract token missing: {token}')
for bad in ('fwrite(&core->runtime','fwrite(&core->ppu','fwrite(&core->devices','memcpy(payload, &core','runtime opcode','interpreter fallback'): ck(bad not in core,f'unsafe V09 source pattern: {bad}')
# Frontends may depend on public/common boundary only, never private machine headers.
fronts=[root/'frontend/common/ai2_frontend_session.c',root/'frontend/headless/ai2_headless.c',root/'frontend/headed/ai2_headed_boundary.c']
for f in fronts:
    t=f.read_text()
    for bad in ('src/runtime','src/ppu','src/apu','src/mapper','Ai2V05Runtime','Ai2V07Ppu','Ai2V08Apu','ai2_v06_','ai2_v07_','ai2_v08_'): ck(bad not in t,f'frontend private access: {f.name}: {bad}')
ck('ai2_frontend_session_advance' in fronts[1].read_text() and 'ai2_frontend_session_advance' in fronts[2].read_text(),'headed/headless do not share common frame adapter')
ck('ai2_static_core_advance_frame' in fronts[0].read_text(),'common adapter does not call one public frame API')
# No target ROM or common cache/build residue may live in source.
for f in root.rglob('*'):
    if not f.is_file(): continue
    rel=f.relative_to(root).as_posix()
    ck(f.suffix.lower() not in ('.nes','.fds','.unf','.unif'),f'ROM-format file in project: {rel}')
    ck('__pycache__' not in rel and not rel.endswith('.pyc'),f'Python cache residue: {rel}')
    if f.stat().st_size==262160:
        try: ck(sha(f)!='10dd53fd62c95e77856b09c693b3bdd14b62a89988902eea111257cafe9b8acc',f'exact target ROM in project: {rel}')
        except OSError: pass
if fail:
    print('V09 contract/static-source audit: FAIL')
    for x in fail: print(' -',x)
    raise SystemExit(1)
print(f'V09 contract/static-source audit: PASS ({len(contract["source_sha256"])} pinned core inputs; opaque/shared/atomic/hook/ROM-exclusion checks)')
