#!/usr/bin/env python3
"""Verify a deterministic full-tree SHA-256 source manifest and detect extras/missing files."""
from __future__ import annotations
import argparse, hashlib, json
from pathlib import Path


def sha256(path: Path) -> str:
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument('root',type=Path)
    ap.add_argument('manifest',type=Path)
    args=ap.parse_args()
    root=args.root.resolve(); manifest=args.manifest.resolve()
    d=json.loads(manifest.read_text(encoding='utf-8'))
    listed={e['path']:e for e in d['files']}
    actual={p.relative_to(root).as_posix():p for p in root.rglob('*') if p.is_file() and p.resolve()!=manifest}
    if set(listed)!=set(actual):
        missing=sorted(set(listed)-set(actual)); extra=sorted(set(actual)-set(listed))
        print('FAIL: manifest path set mismatch')
        if missing: print('missing: '+', '.join(missing))
        if extra: print('extra: '+', '.join(extra))
        return 1
    canonical=[]
    for rel in sorted(listed):
        p=actual[rel]; e=listed[rel]; digest=sha256(p); size=p.stat().st_size
        if digest!=e['sha256'] or size!=e['bytes']:
            print(f'FAIL: content mismatch {rel}')
            return 1
        canonical.append(f"{digest}  {size}  {rel}\n")
    aggregate=hashlib.sha256(''.join(canonical).encode()).hexdigest()
    if aggregate!=d['aggregate_sha256']:
        print('FAIL: aggregate mismatch')
        return 1
    print(f"PASS: package matches manifest ({len(listed)} files, aggregate={aggregate})")
    return 0
if __name__ == '__main__':
    raise SystemExit(main())
