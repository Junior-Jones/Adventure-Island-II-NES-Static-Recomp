# Adventure Island II NES Static Recomp - Version 06 Report

## Result

Version 06: COMPLETE for the `Starter NES(4)` V06 milestone scope. Overall static-core construction: NOT COMPLETE. Gameplay qualification: NOT AUTHORIZED.

## Completed scope

V06 provides the concrete 2A03 CPU state and all 99 V05-required fixed semantic forms, the CPU bus, RAM mirrors/open bus, shared Sharp MMC3B runtime, stack and interrupt handling, cycle-order/dummy-read semantics, target-specific diagnostic traps, and a single CPU/PPU/APU/DMA/mapper scheduler. PPU/APU/DMA device behavior is deliberately not fabricated; those accesses fail closed until V07/V08 supply their modules.

The current V04 graph contains 16,544 accepted instruction identities, 404 finite indirect targets, 1,133 return continuations, zero accepted unofficial opcode forms and zero accepted BRK contexts. No interpreter or runtime opcode-decoder path has been added.

## Construction tests

The primary V06 runtime suite passes 4,459,662 checks under strict GCC and independently passes the same 4,459,662 checks under strict Clang. It includes exhaustive binary ADC/SBC state-space checks and CPU/bus/stack/interrupt/addressing/order tests.

The all-form gate compiles and runs every one of the 99 fixed semantic forms under GCC and Clang, 297 checks per compiler. The static-source gate confirms the required unofficial-form count and admitted BRK-context count are both zero and confirms runtime opcode fetch/decode/interpreter signatures are absent.

A Clang AddressSanitizer + UndefinedBehaviorSanitizer smoke build passes 141 focused runtime checks.

The deterministic no-input static reset frontier is identical across two runs and stops at the first PPU-owned operation, `$E014 STX $2000`, with a `PPU_NOT_CONSTRUCTED` diagnostic at CPU/PPU/APU counters 19/57/19.

All predecessor construction gates remain clean: V01 exact/negative ROM validation; V02 mapper contract and 1,869,243-check exhaustive mapper suite; V03 563-check inventory; V04 272,518-check control-flow suite with `gameplay_seeds=0`; V05 35,542-check generator and 16-check linked dispatch contract. The active Starter package manifest and pinned MesenCE oracle self-test pass.

## Four closure claims

Cold-boot closure: NOT CLOSED. V06 establishes the first deterministic frontier at the V07 PPU boundary.

CPU graph closure: CURRENT V04 ACCEPTED GRAPH CLOSED. ROM-wide fixed-point closure is not yet claimed.

Machine closure: PARTIAL. V06 CPU/bus/scheduler/mapper runtime is complete; V07 PPU and V08 APU/DMA/input remain.

Qualification closure: NOT AUTHORIZED before V10.

## Next milestone

V07 constructs the PPU: dot/scanline/region timing, vblank/NMI behavior, `$2002/$2005/$2006/$2007` semantics, VRAM/palette/OAM, scrolling, background/sprite fetch/evaluation, sprite zero/overflow/clipping, mapper-visible PPU fetch/A12 activity and deterministic 256x240 frame output. Static cold boot can then continue from the exact `$E014 -> $2000` frontier without gameplay-derived code discovery.
