# Version 03 ROM-wide disassembly inventory

Version 03 follows Starter NES Revision 3's construction boundary exactly. Every byte
of the 128 KiB PRG-ROM begins as data/unclassified. Code is rooted only at the three
architectural interrupt vectors under the V02 reset mapping. V03 performs straight-line
decoding through the first control-transfer instruction and deliberately does not
follow that transfer. Direct/indirect edge closure belongs to V04.

## Physical PRG inventory

The cartridge has sixteen 8 KiB physical PRG banks numbered 0 through 15 by V02.
`generated/v03/prg-bank-inventory.json` records every bank's physical byte range,
SHA-256 and classification counts. All 131,072 PRG bytes are covered exactly once by
`generated/v03/classification-ranges.csv`.

Primary V03 classifications are intentionally conservative:

- code: 16 bytes / 10 accepted instructions;
- vector: 6 bytes, the NMI/RESET/IRQ vector table;
- text: 16 bytes, one unique embedded cartridge-title signature adjacent to the
  vector area;
- unclassified: 131,034 bytes;
- padding: 0 bytes. V03 never calls an unobserved byte padding.

The table/pointer evidence is recorded separately in `data-evidence.csv`: the six-byte
interrupt-vector table and its three two-byte architectural pointers are proven. The
unique title signature is the only text range promoted from unclassified. No PRG byte
is labeled a graphics reference because V03 has no proven use-site yet; uncertain
bytes remain unclassified rather than guessed.

## Rooted instruction identities

The fixed MMC3B reset mapping maps CPU $E000-$FFFF to physical PRG bank 15. The three
roots are therefore unambiguous physical identities:

- RESET: PRG15:$1FAE at CPU $FFAE;
- NMI: PRG15:$00CE at CPU $E0CE;
- IRQ: PRG15:$0152 at CPU $E152.

The resulting V03 instruction ledger contains ten instructions: one from RESET, eight
from NMI, and one from IRQ. Every record carries physical bank, bank offset, CPU PC,
length, mnemonic, addressing mode, official/unofficial status, control-flow class,
and a SHA-256 of the source instruction bytes. Raw ROM bytes are not copied into the
ledger or backup.

V03 stops at the RESET direct JMP, the first NMI conditional branch, and the IRQ RTI.
This creates four V04 seeds: the RESET jump target, the NMI taken target, the NMI
fallthrough target, and the RTI dynamic/stack return target. No V04 traversal has been
performed yet.

## Decoder authority and reproducibility

`config/2a03-opcodes.json` contains exactly 256 Ricoh 2A03 opcode metadata records.
The names, addressing modes and unofficial flags were extracted from the pinned
MesenCE 2.2.1 Starter source `Core/NES/Debugger/NesDisUtils.cpp`; Linux and Windows
copies have identical SHA-256
`6b4a4897e3f834667d8f7dcd5e51bba20afc69d9528ac0e3a9f12d90b20bff24`.
Mesen is metadata/reference authority only and does not discover the game code map.

`tools/disassemble_program_map.py` is Version `0.3.0-v03`. The construction test runs
two independent clean generations and requires every emitted file to be byte-identical
to the checked-in generated V03 tree.

## V03 boundaries

There are no decode conflicts or overlapping accepted instructions. The open entries
in `UNRESOLVED-BOUNDARIES.csv` are intentional V04 ownership, not V03 incompleteness.
Production lowering is still forbidden until V05, the V05 generator remains fail-
closed, and gameplay verification remains forbidden until V10 construction closure.
