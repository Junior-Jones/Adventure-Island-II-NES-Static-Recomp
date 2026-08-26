# Adventure Island II NES Static Recomp - Version 08 Report

## Result

Version 08: COMPLETE for the `Starter NES(4)` V08 milestone scope. Overall static-core construction: NOT COMPLETE. Gameplay qualification: NOT AUTHORIZED.

## Completed scope

V08 constructs the target NTSC APU, OAM/DMC DMA and two standard controller ports behind the V06 common scheduler and V07 PPU. Five audio channels exist: two pulse channels, triangle, noise and DMC. Frame sequencing/IRQ, `$4015`, delayed `$4017`, DMC address/length/rate/loop/IRQ, nonlinear mixing, deterministic 44.1 kHz PCM buffering, `$4014` OAM DMA and serial `$4016/$4017` controller behavior are implemented.

The V04 static instruction ledger contains 17 accepted direct/indexed CPU-I/O contexts based at `$4000-$4017`, including ten indexed audio-register stores. The machine implements the complete target NES APU register space so indexed channel selection cannot fall into an unimplemented device stub. NES-TLROM-03 has no expansion audio.

## Construction tests

The strengthened V08 machine suite passes 8,573 checks under strict GCC, independently under strict Clang, and under Clang AddressSanitizer + UndefinedBehaviorSanitizer. It covers all 256 controller button bit patterns, every DMC sample-address and sample-length register value, all DMC/noise period-table selectors, all 32 length-table entries across target channels, both OAM DMA alignment lengths, DMC loop/IRQ and address wrapping, DMA collision arbitration, channel/status/frame-IRQ behavior, mixing and bounded deterministic PCM.

The shared V06 CPU runtime still passes 4,459,662 exhaustive checks under GCC and independently under Clang after V08 DMA arbitration was added. The V07 PPU still passes 357,433 checks under GCC, Clang and Clang ASan+UBSan. V08 static-demand and static-source audits pass with `gameplay_seeds_used=0`.

The deterministic no-input frame-zero endurance test runs 2,000,000 generated static instructions twice with byte-identical results and no trap. It reaches 206 completed frames, 6,150,905 CPU cycles, 18,452,715 PPU cycles, 3,184 controller reads, 398 emulated controller latches and 199 OAM DMA transfers. The current identity is fixed bank 15 `$E127`. No controller/gameplay input is supplied.

This endurance run is vertical machine evidence only. Cold-boot closure is not declared because the active Starter additionally requires bounded oracle checkpoint agreement.

## Four closure claims

Cold-boot closure: NOT CLOSED. Native no-input construction is trap-free beyond 200 frames, but bounded oracle checkpoint agreement has not yet been performed.

CPU graph closure: CURRENT V04 ACCEPTED GRAPH CLOSED. ROM-wide V10 fixed-point construction is not yet claimed.

Machine closure: V08 target-required CPU/mapper/PPU/APU/DMA/controller semantics are constructed. V09 still owns the opaque public API, snapshot/persistence boundary and observation hooks, and V10 owns the final ROM-wide construction audit.

Qualification closure: NOT AUTHORIZED before V10 `STATIC_CORE_COMPLETE`.

## Next milestone

V09 hides machine layout behind an opaque core handle, exposes reset/frame/video/audio/input/trap APIs, implements versioned ROM/core-bound atomic snapshots at safe boundaries, adds filtered non-reentrant observation hooks, and connects headed/headless callers to one frame-advance path. Construction remains static and gameplay-free.
