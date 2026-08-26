# Version 01 completion report

Version 01 establishes exact cartridge authority and a clean workspace only. It
contains no translated game code and makes no gameplay-completeness claim.

The exact external ROM is pinned by file SHA-256 and headerless payload SHA-256.
Its iNES header declares Mapper 4, 128 KiB PRG-ROM, 128 KiB CHR-ROM, horizontal
initial mirroring, no trainer and no battery. Independent physical-cartridge data
matches both PRG and CHR CRCs and identifies NES-TLROM-03 with MMC3B, 0 KiB WRAM
and 0 KiB VRAM. The legacy header does not encode a NES 2.0 submapper, so no
submapper value is invented.

The vector table in the fixed final 8 KiB PRG bank is `CE E0 AE FF 52 E1`, giving
NMI `$E0CE`, RESET `$FFAE`, and IRQ `$E152`.

Construction validation accepts the exact supplied ROM and rejects a one-byte
mutation, a malformed magic header, a truncated image, and an image with trailing
bytes. The packaged Linux MesenCE 2.2.1 NES-only oracle also passes its self-test.
No gameplay route was run because Starter NES forbids gameplay verification before
V10 construction closure.

Next milestone: V02 must define the complete TLROM/MMC3B mapper contract: power and
reset mapping, all PRG/CHR windows, bank-register transitions, mapper-controlled
mirroring, write decoding, PRG-RAM absence/protection behavior, IRQ/A12 rules, and
physical bank-numbering functions shared by later disassembly and runtime stages.
