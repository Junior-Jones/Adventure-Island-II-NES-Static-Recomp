#!/usr/bin/env python3
"""Create a deterministic full-tree SHA-256 manifest for an Adventure Island II milestone."""
from __future__ import annotations
import argparse, hashlib, json
from pathlib import Path


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('root', type=Path)
    ap.add_argument('output', type=Path)
    ap.add_argument('--milestone', required=True)
    args = ap.parse_args()
    root = args.root.resolve()
    output = args.output.resolve()
    entries=[]
    for p in sorted((x for x in root.rglob('*') if x.is_file() and '.git' not in x.relative_to(root).parts), key=lambda x: x.relative_to(root).as_posix()):
        if p.resolve() == output:
            continue
        rel=p.relative_to(root).as_posix()
        if '/__pycache__/' in f'/{rel}/' or rel.endswith('.pyc'):
            raise SystemExit(f'transient Python cache present: {rel}')
        if rel.startswith('build-') or '/build-' in rel:
            raise SystemExit(f'transient build artifact present: {rel}')
        entries.append({'path':rel,'bytes':p.stat().st_size,'sha256':sha256(p)})
    canonical=''.join(f"{e['sha256']}  {e['bytes']}  {e['path']}\n" for e in entries).encode()
    data={
        'schema':1,
        'project':'Adventure Island II NES Static Recomp',
        'milestone':args.milestone,
        'file_count':len(entries),
        'aggregate_method':'sha256 of UTF-8 lines: <file_sha256><two spaces><bytes><two spaces><path><newline>, path-sorted',
        'aggregate_sha256':hashlib.sha256(canonical).hexdigest(),
        'excluded_self':output.relative_to(root).as_posix(),
        'files':entries,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(data,indent=2,sort_keys=False)+'\n',encoding='utf-8')
    print(f"PASS: wrote {args.milestone} source manifest with {len(entries)} files aggregate={data['aggregate_sha256']}")
    return 0
if __name__ == '__main__':
    raise SystemExit(main())
