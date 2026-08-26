#!/usr/bin/env python3
from __future__ import annotations
import json
from pathlib import Path

def load(p: Path):
    return json.loads(p.read_text())

def require(cond, name, checks):
    checks.append({'name': name, 'result': 'PASS' if cond else 'FAIL'})
    if not cond:
        raise AssertionError(name)

def main():
    root = Path(__file__).resolve().parents[2]
    r = root / 'receipts' / 'v10.01'
    checks=[]
    required=[
      'CMakeLists.txt',
      'frontend/common/ai2_frontend_session.c',
      'frontend/common/ai2_frontend_input.c',
      'frontend/common/ai2_frontend_capture.c',
      'frontend/headless/ai2_headless.c',
      'frontend/linux/ai2_linux_x11.c',
      'frontend/linux/ai2_linux_audio.c',
      '00-START-HERE/ADVENTURE-ISLAND-II-V10.01-MASTER-HANDOFF.md'
    ]
    require(all((root/p).is_file() for p in required),'frontend_sources_and_handoff_exist',checks)
    static=load(r/'V10.01-STATIC-INHERITANCE-GATE.json')
    require(static.get('result')=='PASS' and static.get('checks_passed')==25 and static.get('compile_invoked') is False,'static_inheritance_25_of_25',checks)
    natural=load(r/'V10.01-NATURAL-TEST-SUMMARY.json')
    require(natural.get('result')=='PASS' and len(natural.get('runs',[]))==3,'natural_ladder_present',checks)
    require([x['fields'].get('status') for x in natural['runs']]==['ok','ok','ok'],'natural_ladder_status_ok',checks)
    require([x['fields'].get('frames') for x in natural['runs']]==['300','902','5409'],'natural_ladder_frames',checks)
    shots=load(r/'V10.01-MESEN-SCREENSHOT-COMPARISON.json')
    require(shots.get('result')=='PASS' and shots.get('mesen_core_modified') is False,'mesen_comparison_and_core_unchanged',checks)
    require(shots.get('minimum_exact_percent',0.0)>=99.9,'mesen_phase_aligned_pixels_ge_99_9_percent',checks)
    audio=load(r/'V10.01-AUDIO-CAPTURE.json')
    require(audio.get('result')=='PASS' and audio.get('non_silent') is True and audio.get('clipped_sample_count')==0,'wav_non_silent_unclipped',checks)
    refs=load(r/'V10.01-REFERENCE-FRONTEND-AUDIT.json')
    require(refs.get('production_imported_game_specific_reference_core_code') is False,'reference_core_not_imported',checks)
    require(not (root/'STATIC_CORE_COMPLETE').exists(),'formal_static_complete_not_overclaimed',checks)
    out={'schema':1,'version':'10.01','result':'PASS','checks_passed':len(checks),'checks':checks}
    (r/'V10.01-FRONTEND-QUALIFICATION-GATE.json').write_text(json.dumps(out,indent=2)+'\n')
    print(json.dumps({'result':'PASS','checks_passed':len(checks)},sort_keys=True))
if __name__=='__main__': main()
