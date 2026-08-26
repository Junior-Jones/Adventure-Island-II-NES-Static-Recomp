# V02 Adventure Island II board and mapper contract

## Authority and target

This document is the Version 02 mapping authority for the exact pinned
`Adventure Island II (USA)` ROM. The legacy iNES header selects Mapper 4. Physical
cartridge evidence matching PRG CRC32 `6FA0E650`, CHR CRC32 `1DDB1C0E`, and combined
payload CRC32 `771C8855` identifies Nintendo PCB `NES-TLROM-03` with a Sharp MMC3B,
128 KiB PRG-ROM, 128 KiB CHR-ROM, no PRG RAM, no CHR RAM, no battery, and
mapper-controlled mirroring.

The legacy iNES image has no NES 2.0 submapper field. No submapper number is invented.
The exact physical board/chip evidence is sufficient to select standard TLROM plus
Sharp/new MMC3 IRQ behavior and to exclude MMC6/HKROM, mapper 118, mapper 119, and
unrelated MMC3 clones for this target.

The production mapping authority is `src/mapper/ai2_mmc3_mapper.c`. MesenCE and
external documentation are evidence/oracles only and are never production fallback.

## Physical numbering

PRG-ROM is numbered as sixteen physical 8 KiB banks, 0 through 15. Physical PRG bank
N contains PRG payload offsets `N*0x2000` through `N*0x2000+0x1FFF`. CPU reads use
`physical_bank*0x2000 + (cpu_address & 0x1FFF)` as the PRG payload offset.

CHR-ROM is numbered as 128 physical 1 KiB banks, 0 through 127. Physical CHR bank N
contains CHR payload offsets `N*0x0400` through `N*0x0400+0x03FF`. PPU pattern reads
use `physical_bank*0x0400 + (ppu_address & 0x03FF)` as the CHR payload offset.

MMC3 PRG registers R6/R7 expose six bank bits, but this 128 KiB board has only sixteen
8 KiB banks; physical selection therefore uses the low four installed-bank bits.
CHR selection uses seven installed-bank bits. R0/R1 are forced even for their 2 KiB
pairs.

## Power and reset

The Sharp MMC3B switchable latches do not have a useful guaranteed deterministic
power-on value. V02 therefore represents `$8000`, R0-R7, `$A000`, `$A001`, IRQ state,
and initial A12-filter state as explicitly unknown at cold power. A read depending on
one of those unknown latches returns `unresolved-power-state`; the static core must
not guess a default.

The fixed `$E000-$FFFF` PRG window is physical bank 15 independent of PRG mode and is
therefore valid immediately at power. This guarantees the reset vector at
`$FFFC-$FFFD` is readable from physical bank 15. The exact ROM vectors remain NMI
`$E0CE`, RESET `$FFAE`, and IRQ `$E152`.

The standard NES cartridge edge connector and MMC3 do not provide a dedicated CPU
reset input to reset mapper latches. `ai2_mmc3_cpu_reset` therefore preserves mapper
state. This is distinct from cold power-on, where the latches are explicitly unknown.

## CPU cartridge address contract

`$0000-$401F` is console-owned and returns `not-cartridge` from the mapper authority.
`$4020-$5FFF` has no populated cartridge expansion hardware on this board and resolves
as open bus. `$6000-$7FFF` is the standard PRG-RAM window, but the exact TLROM-03 board
has no PRG-RAM chip, so reads remain open bus regardless of `$A001`.

For `$8000-$FFFF`, PRG mode is `$8000` bit 6. In mode 0, `$8000-$9FFF` is R6,
`$A000-$BFFF` is R7, `$C000-$DFFF` is fixed physical bank 14, and `$E000-$FFFF` is
fixed physical bank 15. In mode 1, `$8000-$9FFF` is fixed bank 14,
`$A000-$BFFF` remains R7, `$C000-$DFFF` becomes R6, and `$E000-$FFFF` remains fixed
bank 15.

All CPU addresses in `$8000-$FFFF` are also mapper-write aliases. Write decoding is
exactly `address & 0xE001` and has no bus conflicts on this board.

## PPU cartridge address contract

`$0000-$1FFF` is CHR-ROM in eight 1 KiB slots. With `$8000` bit 7 clear, the slots are
R0-even, R0+1, R1-even, R1+1, R2, R3, R4, R5. With bit 7 set, the order is R2, R3,
R4, R5, R0-even, R0+1, R1-even, R1+1.

`$2000-$3EFF` resolves through the mapper-controlled CIRAM nametable connection.
`$3000-$3EFF` mirrors `$2000-$2EFF`. `$A000` bit 0 equal to 0 selects vertical
mirroring (`CIRAM A10 <- PPU A10`); bit 0 equal to 1 selects horizontal mirroring
(`CIRAM A10 <- PPU A11`). `$3F00-$3FFF` is PPU palette RAM and returns
`not-cartridge` from this mapper authority.

The iNES header's horizontal mirroring metadata is recorded as identity evidence but
is not used to invent a guaranteed MMC3 `$A000` power-on latch value.

## Complete register and transition contract

`$8000-$9FFE` even is bank select. D0-D2 select R0-R7; D6 immediately controls PRG
mode; D7 immediately controls CHR inversion; D3-D5 are ignored for standard MMC3.

`$8001-$9FFF` odd is bank data. It writes the selected register. R0/R1 ignore D0 and
remain even. R6/R7 ignore D7-D6. Installed ROM capacity then masks physical bank
selection to this target's sixteen PRG banks or 128 CHR banks. A bank-data write made
before the bank-select latch is known is deliberately fail-closed as ambiguous rather
than assigned to a guessed register.

`$A000-$BFFE` even is nametable arrangement. D0=0 selects vertical mirroring and D0=1
selects horizontal mirroring; other bits are ignored.

`$A001-$BFFF` odd is PRG-RAM protect/control. D7 is MMC3 RAM enable and D6 is write
protect. V02 latches these bits for faithful state/debugging, but no RAM is populated,
so `$6000-$7FFF` remains open bus.

`$C000-$DFFE` even replaces the IRQ reload/latch value and does not immediately
replace the current counter.

`$C001-$DFFF` odd clears the current IRQ counter to zero and requests a reload on the
next qualified PPU A12 rising edge.

`$E000-$FFFE` even disables mapper IRQ generation and acknowledges/clears the pending
external mapper IRQ. Counter operation itself is not stopped.

`$E001-$FFFF` odd enables mapper IRQ generation without modifying the current counter.

## Sharp MMC3B IRQ contract

The exact USA cartridge uses a Sharp MMC3B, so the new/Sharp zero-counter behavior is
a target fact rather than a generic Mapper-4 guess. The IRQ counter clocks only on a
qualified PPU A12 low-to-high transition after A12 has remained low across at least
three M2 falling edges; it is not clocked from a host scanline counter.

On a qualified edge, if the counter is zero or reload is pending, the counter loads
the latch; otherwise it decrements. Reload-pending is then cleared. With Sharp
behavior, if the resulting counter is zero and IRQ generation is enabled, the mapper
asserts its IRQ. Consequently, latch value zero can produce an IRQ on every qualified
A12 edge while enabled. `$E000` is the explicit acknowledgement path.

The construction suite exhaustively tests all 256 latch values, all 256 counter
values, both reload states, and both enable states for one qualified counter clock,
in addition to the three-M2 A12 qualification sequence.

## Other board properties

There is no PRG RAM, PRG NVRAM, CHR RAM, or CHR NVRAM. There are no mapper bus
conflicts. There is no expansion audio. Four-screen nametable RAM is not populated.

## Fail-closed boundary

Unspecified power-on latch values are modeled as nondeterministic/unknown state, not
as an unresolved hardware choice. Operations depending on unknown latches return an
explicit unresolved result until software writes establish the required state. V02
has no remaining ambiguous target-hardware blocker.

A later static-analysis milestone must reopen this contract if ROM-wide evidence
proves the game depends on a silicon corner case not represented here. That rule does
not authorize V03/V04 to change mapper behavior silently; any such change requires a
new evidence receipt and regression of the complete V02 suite.

## V02 exit proof

The construction test exhaustively decodes all 65,536 CPU write addresses, checks
complete CPU and PPU read-address coverage under a fully established mapper state,
exercises both PRG and CHR mode layouts, all byte values of PRG/CHR bank registers,
all mapper register state transitions, target RAM absence and mirroring, the complete
Sharp IRQ counter state space, reset preservation, unknown-power fail-closed behavior,
and exact-ROM vector mapping through physical PRG bank 15.

The machine-readable authority is `config/mapper-contract.json`; the test authority
is `tests/construction/test_v02_mapper.c` plus
`tests/construction/test_v02_mapper_contract.py`.
