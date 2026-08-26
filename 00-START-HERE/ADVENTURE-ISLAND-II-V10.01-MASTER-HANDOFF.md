# Adventure Island II NES Static Recomp — V10.01 master handoff

## Start here

This file is the project-specific continuation authority created because the inherited V10 checkpoint did not contain a dedicated Adventure Island II master handoff. Read this file first, then `README.md`, `docs/STATIC-CORE-STATUS.md`, `docs/V10-REPORT.md`, `docs/V10.01-FRONTEND-NATURAL-QUALIFICATION.md`, `docs/UNRESOLVED-BOUNDARIES.csv`, and the V10/V10.01 receipts.

## Project boundary

The production static core is Adventure Island II-specific. Do not substitute game-specific code or execution authority from Mega Man 6, Battletech C64, Starter templates, Mesen, or any other project. Reference projects may be consulted only for architecture, frontend organization, host integration, test/capture methods, or repair ideas. Mesen is an independent oracle only.

The commercial Adventure Island II ROM is external test input and must never be put into project/release ZIPs. Exact expected ROM SHA-256 is `10dd53fd62c95e77856b09c693b3bdd14b62a89988902eea111257cafe9b8acc`.

## Static-core state inherited from V10

V10 ROM-wide proof owns all 131,072 PRG bytes: 35,423 accepted instruction bytes, 244 inline-data bytes, 22 metadata bytes, and 95,383 statically excluded immutable-data bytes. The finite graph contains 16,544 accepted identities and 19,833 edges with zero unresolved PRG bytes and zero unresolved current-graph edges. Executable mapper authority is R7 banks 10-13 plus fixed banks 14-15; banks 0-9 and the R6 `$8000-$9FFF` window are proved non-executable under the closed graph. Generated execution remains fail-closed and has no runtime opcode interpreter/fallback.

V10 corrected the PPU reset starting phase to scanline 0, dot 4 from source timing proof. The V10.01 inheritance gate preserves those static claims and passes 25/25 without invoking compilation.

## What V10.01 added

V10.01 completed the requested frontend application layer without changing static execution authority. Mega Man 6 source was used to understand the NES core-to-frontend contract. Battletech C64 source was used to understand a fuller frontend/headless application organization. Only architecture and host-integration ideas were taken.

The project now has:

- root CMake build for the existing Adventure Island II core;
- shared frontend session/input/capture code;
- `ai2-headless` with exact frame/emulated-time limits, screenshot capture, continuous PCM draining, WAV capture and final status;
- Linux/X11 `ai2-linux` headed frontend with keyboard input and optional deterministic frame limit;
- Linux PulseAudio output adapter loaded dynamically when available.

Keyboard mapping in `ai2-linux`: arrows = D-pad, Z = A, X = B, Enter = Start, Shift = Select, P = pause, Escape = quit.

## Qualified runtime results

Final warning-clean native build:

- 300 frames: PASS, 8,931,650 total CPU cycles, 2,905,191 executed static instructions, 220,075 PCM samples drained, status `ok`.
- 15-second target: PASS, 902 frames, 15.007212647 emulated seconds, 8,706,887 static instructions, 661,818 PCM samples drained, status `ok`.
- 90-second target: PASS, 5,409 frames, 90.000369321 emulated seconds, 52,185,796 static instructions, 3,969,016 PCM samples drained, status `ok`.
- Linux/X11 smoke: PASS for 60 frames under Xvfb.

Final Mesen phase-aligned screenshot comparison:

- native frame 299 vs Mesen endFrame 300: 99.9267578125% exact pixels;
- native frame 901 vs Mesen endFrame 902: 99.951171875% exact pixels;
- native frame 5408 vs Mesen endFrame 5409: 99.9918619792% exact pixels.

Mesen's endFrame capture exposes the completed framebuffer one callback later than the native frontend copy point. The stripped oracle launcher also required its default NES 2C02 palette to be seeded instead of leaving `UserPalette` all zeroes. The Mesen core `.so` itself remained unchanged with SHA-256 `060c4a4111d0770a65f88aaecbca7faa8c9ea8edcc081dd141d2fe9b8792a0e7`.

Final 90-second WAV: 44.1 kHz mono signed 16-bit PCM, 3,969,016 samples, 90.0003628118 seconds, RMS 6,132.829, peak 18,324, zero clipped samples. SHA-256 `f3451523fae7a8077ed639ffa38631fd30e671e6647dff36f565af6f3955510c`.

## Important closure distinction

V10.01 frontend/natural qualification is complete. `STATIC_CORE_COMPLETE` is still intentionally absent. Do not create it merely because the long natural run, screenshots and WAV pass. Starter NES requires the stricter bounded fresh native-versus-oracle cold-boot differential for CPU state, important RAM, mapper state, PPU events and audio timing. That formal state differential is the remaining static-construction closure item.

Natural execution in V10.01 was explicitly authorized by the user and is qualification evidence only. Never use gameplay/natural behavior to invent static code targets or replace ROM/source proof.

## Next work

The next static task is to build a finite V10.02/V10.01-closeout cold-boot state-differential harness using the already working frontend/core API and the independent Mesen oracle. Capture matching checkpoints from reset/frame-zero through the stable cold-boot loop, compare CPU registers/cycles, selected important RAM, MMC3 registers/bank mapping, PPU timing/events, and APU/audio timing. Repair only genuine Adventure Island II source/semantic defects exposed by that comparison. Re-run the V10.01 static inheritance gate and relevant frontend natural smoke after any source change.

Only after every Starter cold-boot closure field is supported may `STATIC_CORE_COMPLETE` be written. Post-core gameplay qualification should remain distinct from static construction authority.

## Key receipts

Read these machine-readable records:

- `receipts/v10.01/V10.01-STATIC-INHERITANCE-GATE.json`
- `receipts/v10.01/V10.01-BUILD-QUALIFICATION.json`
- `receipts/v10.01/V10.01-NATURAL-TEST-SUMMARY.json`
- `receipts/v10.01/V10.01-MESEN-SCREENSHOT-COMPARISON.json`
- `receipts/v10.01/V10.01-AUDIO-CAPTURE.json`
- `receipts/v10.01/V10.01-REFERENCE-FRONTEND-AUDIT.json`

The raw run captures are archived outside the GitHub source package. Their compact machine-readable qualification summaries and hashes remain under `receipts/v10.01/`.
