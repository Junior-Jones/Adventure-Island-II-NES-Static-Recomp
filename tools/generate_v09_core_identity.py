#!/usr/bin/env python3
import hashlib,json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
ROM='10dd53fd62c95e77856b09c693b3bdd14b62a89988902eea111257cafe9b8acc'
STARTER='a895ce1d21ce41d10ef9393b47bdb2f315db42e1a6af2477f9b79a03a1923703'
V08_MANIFEST='b968c9f6d182942dad2d1b5609a57c80eab71ceea3b0d34b5824e7ddf1100a71'
files=[
'include/ai2_static_core.h','src/api/ai2_v09_core.c','src/api/ai2_sha256.c','src/api/ai2_sha256.h',
'src/runtime/ai2_v06_runtime.c','src/runtime/ai2_v06_runtime.h','src/runtime/ai2_v06_semantics.c',
'src/ppu/ai2_v07_ppu.c','src/ppu/ai2_v07_ppu.h','src/apu/ai2_v08_apu.c','src/apu/ai2_v08_apu.h',
'src/mapper/ai2_mmc3_mapper.c','src/mapper/ai2_mmc3_mapper.h','generated/v05/generated-tree-manifest.json',
'generated/v05/generated-identity.json','config/target-profile.json','config/v08-machine-contract.json']
source={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in files}
authority={
'schema':1,'milestone':'V09','compatibility_contract':'Adventure Island II NES opaque public core API v1',
'rom_sha256':ROM,'starter_nes4_zip_sha256':STARTER,'v08_source_manifest_aggregate_sha256':V08_MANIFEST,
'canonical_snapshot_state_version':1,'snapshot_file_version':1,'region':'NTSC','mapper':'NES-TLROM-03 / Sharp MMC3B',
'executable_ram_epoch_count':0,'source_sha256':source}
canon=json.dumps(authority,sort_keys=True,separators=(',',':')).encode()
identity=hashlib.sha256(canon).hexdigest()
contract=dict(authority)
contract['core_compatibility_sha256']=identity
contract['policy']={
'opaque_machine_layout':True,'one_shared_advance_frame':True,'copied_video':True,'bounded_copied_audio':True,
'controller_path':'host pending masks -> emulated $4016/$4017 live/latch/serial path',
'snapshot':'canonical little-endian fields; ROM/core-bound; validate temporary state before atomic commit',
'hooks':'filtered synchronous observation/control only; non-reentrant; no execution fallback',
'gameplay_used_as_static_authority':False}
(root/'config/v09-core-contract.json').write_text(json.dumps(contract,indent=2,sort_keys=True)+'\n')
def arr(h): return ','.join('0x'+h[i:i+2]+'u' for i in range(0,64,2))
h=f'''#ifndef AI2_V09_IDENTITY_H\n#define AI2_V09_IDENTITY_H\n#define AI2_V09_CORE_COMPAT_SHA256 "{identity}"\n#define AI2_V09_ROM_SHA256 "{ROM}"\nstatic const unsigned char AI2_V09_CORE_COMPAT_DIGEST[32] = {{{arr(identity)}}};\nstatic const unsigned char AI2_V09_ROM_DIGEST[32] = {{{arr(ROM)}}};\n#endif\n'''
(root/'generated/v09').mkdir(parents=True,exist_ok=True)
(root/'generated/v09/ai2_v09_identity.h').write_text(h)
print(identity)
