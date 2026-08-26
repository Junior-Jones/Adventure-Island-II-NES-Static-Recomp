# Adventure Island II NES Static Recomp - Version 07 Report

## Result

Version 07: COMPLETE for the `Starter NES(4)` V07 milestone scope. Overall static-core construction: NOT COMPLETE. Gameplay qualification: NOT AUTHORIZED.

## Completed scope

V07 constructs the target NTSC NES PPU behind the V06 bus/scheduler: all eight CPU-visible PPU registers and mirrors, address/scroll latches, buffered PPUDATA behavior, CIRAM/palette/OAM, MMC3-mapped CHR ROM, dot/scanline/vblank/NMI timing, odd-frame timing, background fetch/scroll state, 8x8 and 8x16 sprite fetch/evaluation, priority, sprite-zero/overflow state, mapper-visible PPU A12 accesses, and stable 256x240 indexed/BGRA frame-copy APIs.

The construction remains static. The V04 instruction ledger contains 56 direct PPU-register contexts and 17 direct V08 I/O contexts. No gameplay-derived code seed or screenshot-based coverage claim is present.

## Construction tests

The V07 PPU suite passes 357,433 checks under strict GCC, independently passes the same 357,433 checks under strict Clang, and passes the same suite under Clang AddressSanitizer + UndefinedBehaviorSanitizer.

The suite exercises CHR/CIRAM/palette mapping, horizontal/vertical mirroring, PPU register/latch behavior, OAM access, PPUDATA buffering, vblank/NMI suppression, scroll transfers, background/sprite priority, target-used 8x16 sprite fetches, sprite-zero/overflow, Sharp MMC3B A12 coupling, odd-frame timing and both frame-copy APIs. Static-demand, static-source and machine-contract gates also pass.

The deterministic no-input cold-reset continuation executes 22,974 generated instructions, completes two PPU frame generations and reaches the first V08-owned operation, physical bank 15 `$E15D: STA $4015` with `$0F`. Two independent runs match exactly at CPU/PPU/APU scheduler counters 80,701 / 242,103 / 80,701. The PPU reports no error.

All predecessor V01-V06 construction gates are replayed before sealing V07. Gameplay remains unused for static construction.

## Four closure claims

Cold-boot closure: NOT CLOSED. V07 removes the PPU as the startup blocker; V08 APU `$4015` is the next explicit frontier.

CPU graph closure: CURRENT V04 ACCEPTED GRAPH CLOSED. ROM-wide fixed-point closure is not yet claimed.

Machine closure: PARTIAL. V06 CPU/bus/scheduler/mapper and V07 PPU are constructed. V08 APU/DMA/input remains.

Qualification closure: NOT AUTHORIZED before V10.

## Recorded accuracy limits

Sprite overflow uses the ninth-in-range condition rather than the transistor-level diagonal overflow bug. OAM analog decay/corruption edge behavior is not modeled. CPU/PPU sub-dot glitches/delays around `$2005/$2006/$2007` are represented at the deterministic scheduler boundary. BGRA conversion is deterministic host output rather than an analog NTSC color certificate; indexed palette/emphasis state is authoritative.

## Next milestone

V08 constructs APU, OAM/DMC DMA and controller input. Static cold boot continues from the exact `$E15D STA $4015` frontier. No gameplay is required or authorized to discover code or close static coverage.
