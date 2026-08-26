# Static core status

- Project: Adventure Island II NES Static Recomp
- Exact ROM SHA-256: `10dd53fd62c95e77856b09c693b3bdd14b62a89988902eea111257cafe9b8acc`
- Payload SHA-256: `5ee479ef04d80366a75dd144cbeb47d0347ee82025d1b65dd358e1f6498c9a39`
- Mapper/board: Mapper 4, NES-TLROM-03, Sharp MMC3B
- Region: NTSC
- Active Starter: `Starter NES(4).zip`, 2026-08-18 construction-flow update
- Product release: 1.0.0
- Current construction milestone: V10.02
- Current result: ROM-wide/static construction side complete; compact frontend integration and requested natural runtime/screenshot/audio qualification complete
- Static construction gate: NOT COMPLETE — full Starter NES cold-boot state differential remains
- Natural runtime validation: COMPLETED by explicit user authorization; not used as static authority

## Closure claims

Cold-boot closure is **not yet closed after the V10 timing correction**. The sealed predecessor proved a long trap-free no-input run, and V10 source proof plus a bounded oracle check correct the reset PPU phase, but the corrected source still needs one fresh native frame-zero certification before predecessor runtime evidence can be replaced.

CPU graph closure is **ROM-WIDE CLOSED by V10 static proof**. All 131,072 PRG bytes are owned: 35,423 code, 244 inline data, 22 metadata and 95,383 statically excluded immutable data, with zero unresolved PRG bytes. The finite graph is 16,544 accepted identities and 19,833 edges with zero unresolved graph edges.

Machine construction is present for CPU/bus/scheduler/Sharp-MMC3B, target PPU, APU/DMA/controllers and opaque public API/snapshot/frontend boundary. V10 additionally proves that executable mapper authority is limited to R7 banks 10-13 plus fixed banks 14-15; banks 0-9 and the R6 CPU window are non-executable under the closed graph.

Formal Starter qualification closure remains open until `STATIC_CORE_COMPLETE` is legitimately written. This construction receipt is tracked separately from the Windows product version.

## V10 non-build gate

The consolidated V10 gate passes 25/25 with no compile and no native runtime execution. It hash-gates 234 unchanged V09 files, permits only the PPU timing source plus its two compatibility-identity outputs to change, clean-regenerates V04/V05/V10 twice, compares complete generated trees, audits fail-closed dispatch/default behavior, checks static opcode-fetch/decode prohibitions and confirms required machine modules exist.

Current compatibility identity after the V10.02 proof-preserving size compaction:

`1e3e374a618dfc1e04357b56f86813c4ca5be94130d7acb4e4c39d81897b5597`

V10.02 preserves all 16,544 admitted instruction identities while replacing
repeated generated switch bodies with exact descriptors and one shared
fail-closed executor. Generated C is 51.80% smaller, the MSVC Release static
library is 56.94% smaller and the Windows launcher is 11.04% smaller. The direct
and compact cores match after 2,000,000 no-input instructions. See
`docs/V10.02-STATIC-CORE-SIZE-COMPACTION.md` and the machine-readable compaction
receipt for the complete measurements and equivalence evidence.

## Remaining boundary

V10.01 has now rebuilt and naturally exercised the corrected core through 300 frames, 15 seconds and 90 seconds, plus phase-aligned Mesen screenshots and a 90-second WAV. The remaining formal construction blocker is narrower: the Starter-required bounded native/oracle CPU/RAM/mapper/PPU-event/audio-timing cold-boot state differential. It is not waived by the successful natural runs.

`STATIC_CORE_COMPLETE` does not exist. Version 1.0.0 is the Windows product release; that product version does not retroactively create or replace the missing Starter construction receipt.
