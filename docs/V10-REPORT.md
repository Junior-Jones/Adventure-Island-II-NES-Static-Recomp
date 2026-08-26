# Version 10 report — static construction closure work

Version 10 is active and its ROM-wide/static side is complete, but the milestone is **not yet sealed** and `STATIC_CORE_COMPLETE` is deliberately absent. The final cold-boot closure gate requires a fresh native build after the V10 reset-phase source correction. Current project policy forbids compiling/building the static core unless explicitly requested, so that one dynamic certification gate is deferred rather than repeated automatically.

## ROM-wide fixed point

`tools/analyze_v10_romwide_closure.py` closes every PRG byte from exact ROM and sealed V04 proof authority only. The complete 131,072-byte PRG ownership is 35,423 accepted instruction bytes, 244 proved inline-data bytes, 22 metadata bytes, and 95,383 formally excluded immutable-data bytes. Unresolved PRG bytes are zero.

The finite graph remains 16,544 accepted physical-bank/PC identities and 19,833 edge records with zero unresolved current-graph edges, zero executable-RAM epochs and zero self-modifying-code epochs.

The mapper execution audit proves every accepted mapper-relevant write. Reachable bank-select writes keep MMC3 PRG mode D6=0. Generic bank helper calls select only R6 or R7. R7 execution is exactly physical banks 10, 11, 12 and 13. There are zero accepted transfers into the R6 `$8000-$9FFF` window. Fixed physical banks 14 and 15 complete the executable domain, so physical PRG banks 0-9 are formally excluded from executable authority.

## Reset-phase source correction

V10 corrects the deterministic PPU power-on phase from the inherited pre-render origin to scanline 0, dot 4. Source proof is finite: `ai2_v06_reset` charges five internal CPU cycles and two vector-read cycles, and the common scheduler advances three PPU dots per CPU cycle. Therefore the RESET entry is reached at dot 25.

A bounded independent MesenCE check agrees: `$FFAE` is reached at CPU cycle 7, scanline 0, PPU cycle 25; `$E010` is reached at CPU cycle 10, scanline 0, PPU cycle 34. This observation qualifies the source-derived timing rule and supplies no static code target or execution authority.

The timing semantic change regenerates the active core compatibility identity. The current identity is `7a4de59cddfda44dd637f2017a76b6bd5dcaf4e1b28299bbf388bbe751dff824`.

## Streamlined non-build certification

`tests/construction/test_v10_nonbuild_gate.py` replaces repeated ancestor rebuilds with a consolidated deterministic gate. The sealed V09 237-file manifest is used as a hash baseline. Exactly three inherited files may differ: the corrected PPU source plus the V09 compatibility-contract JSON and generated identity header that necessarily follow that semantic change. All other inherited V09 files must retain their sealed hashes.

The gate regenerates V04, V05 and V10 twice in fresh temporary directories and requires their full trees to match the checked-in generated authority byte-for-byte. It also audits generated bank dispatch, missing-bank/missing-identity defaults, opcode-form contracts, ROM-read ownership, required machine modules and production/oracle separation. It passes 25 checks with `compile_invoked=false` and `runtime_executed=false`.

This workflow intentionally avoids recompiling unchanged historical authority. If a pinned inherited input changes, the hash gate fails and the affected evidence must be revisited instead of silently reusing it.

A legacy-workflow hazard was found during this prompt: `test_v08_cold_boot.py` performs compilation at module top level, so invoking it with `--help` unexpectedly compiled a temporary harness before failing on the non-ROM argument. That accidental compile is explicitly excluded from evidence. V10 now includes `config/v10-build-policy.json` and the safe `tools/run_v10_nonbuild_gate.sh` entry point; build-capable legacy tests are inventoried and must be read rather than executed during non-build work.

## Receipts

V10 emits deterministic architecture, coverage, dependency, limitation, inheritance, reset-phase and non-build-gate receipts under `receipts/v10/`. The ROM remains external and is never packaged.

## Remaining V10 gate

Cold-boot closure remains open after the timing-source correction. The Starter requires repeated fresh frame-zero native execution reaching the stable loop plus bounded native/oracle agreement for CPU state, important RAM, mapper state, PPU events and audio timing. Because the corrected core has not been rebuilt under the current no-compile policy, the previous V09/V08 native cold-boot evidence cannot be promoted across this semantic source change by hash reuse.

Accordingly `STATIC_CORE_COMPLETE` is not written, gameplay qualification remains unauthorized, and Version 10 is not represented as sealed. When compilation is explicitly requested, the refined workflow should perform one end-of-version build/cold-boot/differential certification rather than repeating compilation during ordinary static-proof prompts.
