# Adventure Island II NES Static Recomp - V07 PPU Construction

## Authority and construction rule

V07 follows the `Starter NES(4).zip` new-core V07 milestone. Construction is based on the exact-ROM static instruction ledger, the V02 Sharp MMC3B mapper authority, the V05 generated instruction identities and the V06 CPU/bus/scheduler. No gameplay route, controller sequence, screenshot, save state or execution trace is used to discover or admit code.

The V04 accepted graph contains 56 direct CPU instruction contexts addressing `$2000-$2007`. Those contexts are inventoried in `generated/v07/ppu-static-demand.json`; the same static scan identifies 17 direct `$4000-$4017` contexts that remain V08 work.

## PPU state and CPU-visible registers

`Ai2V07Ppu` owns PPUCTRL, PPUMASK, PPUSTATUS, OAMADDR, the `v/t/fine-x/write-toggle` scroll/address latches, PPUDATA read buffering, PPU I/O open bus, 2 KiB CIRAM, 32 palette bytes, 256 bytes of primary OAM, 32 bytes of secondary OAM, background and sprite fetch state, timing state and the current 256x240 indexed frame.

The CPU-facing V06 bus mirror for `$2000-$3FFF` is bound to the V07 PPU. `$2002` clears vblank and the write toggle and models the vblank suppression window. `$2005/$2006` update the scroll/address latches. `$2007` provides buffered non-palette reads, immediate palette reads with the underlying nametable buffer fill, and the correct one/32 address increment outside rendering. OAM attribute bits 2-4 are masked on normal OAM writes.

## PPU memory and mapper coupling

Pattern-table accesses are mapped through the V02 MMC3 CHR-bank authority; the target board has CHR ROM, so PPU pattern writes cannot mutate it. Nametable accesses use mapper-controlled horizontal/vertical CIRAM mirroring. Palette aliases `$3F10/$3F14/$3F18/$3F1C` map to `$3F00/$3F04/$3F08/$3F0C`, and palette values are six-bit.

Every PPU memory access passes its address to `ai2_mmc3_observe_ppu_address`, so PPU A12 activity and the Sharp MMC3B IRQ counter share the same mapper object as the generated CPU runtime. During cold startup, code disables MMC3 IRQ output before its latch/counter have all been initialized and then writes PPUADDR values that can produce qualified A12 transitions. V07 preserves that internal uncertainty while IRQ output is definitely disabled. If an ambiguous qualified transition becomes externally observable while IRQ output could be enabled, V07 fails closed rather than guessing a counter value.

## Timing, background and sprites

The target is NTSC: 341 dots per scanline and 262 scanlines. Visible lines are 0-239, line 240 is post-render, lines 241-260 are vblank and line 261 is pre-render. Vblank is asserted at line 241 dot 1 and vblank/sprite flags are cleared on the pre-render line at dot 1. With rendering enabled the odd NTSC frame skips the final pre-render dot, producing alternating 89,341/89,342-dot frame intervals.

The background pipeline performs nametable, attribute, low-pattern and high-pattern fetches with scroll coarse-X/coarse-Y increments, horizontal transfer at dot 257 and vertical transfer on pre-render dots 280-304. Sprite construction supports both 8x8 and the target-used 8x16 mode, horizontal/vertical flip, eight-sprite scanline selection, priority, sprite-zero hit and overflow state. Mapper-visible dummy and pattern fetches are retained for A12 behavior.

## Frame API

`ai2_v07_ppu_copy_indexed` copies exactly 256x240 `uint16_t` values. Bits 0-5 retain the six-bit NES palette value and bits 6-8 retain emphasis. This indexed surface is the deterministic PPU output authority.

`ai2_v07_ppu_copy_bgra` converts that state into stable 32-bit BGRA for a future host frontend. Its palette/emphasis transform is deterministic but is not asserted to be an analog composite-video colorimetry reference.

## Known accuracy limits

The sprite-overflow model raises overflow on the ninth in-range sprite and deliberately does not model the transistor-level diagonal overflow bug. OAM analog decay and rendering-time OAM corruption edge cases are not modeled. CPU writes/reads involving `$2005/$2006/$2007` are resolved at the common deterministic scheduler boundary rather than reproducing every transistor-level sub-dot glitch/delay. BGRA color conversion is a host preview and not an analog NTSC certificate.

These limits are explicit and do not create permissive fallbacks. Unknown mapper/device state that affects target execution remains fatal.

## Deterministic cold-reset continuation

With the V07 PPU bound, the static generated core proceeds from the former `$E014 STX $2000` boundary, crosses both startup vblank waits, RAM clearing and MMC3 initialization, and completes two PPU frame generations without controller input. After 22,974 generated instruction steps it stops at physical bank 15 `$E15D: STA $4015`, value `$0F`, because `$4015` is the first V08-owned APU operation.

The two independent runs are identical at 80,701 CPU cycles, 242,103 PPU cycles and 80,701 APU scheduler cycles. The PPU has no error. This is a static machine-construction frontier, not gameplay qualification.
