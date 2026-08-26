# Version 08 APU, DMA and Input Construction

Version 08 constructs the NTSC 2A03 audio, DMA and standard-controller machine layer required by `Starter NES(4)`. Static construction is derived from the sealed V04 instruction ledger, the V06 common CPU-cycle scheduler, the V07 PPU, and pinned NES/MesenCE hardware references. Gameplay traces, controller routes and screenshots are not construction authority.

The APU provides two pulse channels with envelopes and sweep, triangle linear/length sequencing, noise envelope/LFSR, DMC output/sample addressing/rates/loop/IRQ, the NTSC 4-step and 5-step frame sequencer, `$4015` status/enable behavior and `$4017` delayed frame-counter control. The production machine exposes the entire CPU I/O interval required by the target rather than returning convenient zeroes for unknown audio registers.

OAM DMA is initiated only by emulated `$4014`, copies 256 bytes through the CPU bus into PPU `$2004`, and consumes 513 or 514 scheduler CPU cycles depending on alignment. DMC fetches consume four common-scheduler CPU cycles and share arbitration with OAM DMA. All stolen cycles continue PPU/APU/MMC3 timing; the frontend cannot bypass them.

Controller live state is host-facing pending state only. `$4016` emulated strobe/latch behavior snapshots the two standard pads into serial shift state and `$4016/$4017` reads consume the serial order A, B, Select, Start, Up, Down, Left, Right. There is no direct frontend read of a game input variable.

Audio mixing uses deterministic integer versions of the standard NES nonlinear pulse and TND equations. A fixed 44,100 Hz sample stream is derived from the NTSC CPU clock into a bounded 8,192-sample ring. Overflow is counted and bounded rather than allocating without limit.

Known limits are retained in `config/v08-machine-contract.json`. They include sub-cycle frame-counter and DMA alignment quirks, analog/audio-filter behavior, and non-target controller peripherals. These limits do not create a permissive execution path.
