#!/bin/sh
set -eu
if [ "$#" -ne 1 ]; then
  echo "usage: $0 /path/to/external/Adventure-Island-II.nes" >&2
  exit 2
fi
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ROM=$1
exec python3 "$ROOT/tests/construction/test_v10_01_static_gate.py" \
  --rom "$ROM" --root "$ROOT" --json-out "$ROOT/receipts/v10.01/V10.01-STATIC-INHERITANCE-GATE.json"
