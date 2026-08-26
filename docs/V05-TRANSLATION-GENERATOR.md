# Version 05 reproducible translation generator

V05 converts only the sealed V04 static authority into fixed C. The generator takes the exact external ROM only to revalidate immutable instruction bytes and hashes; it never treats a gameplay trace, save state, screenshot, input route or oracle-observed PC as translation authority.

## Generated authority

Accepted instruction identities: 16,544. Physical-bank distribution: bank 10 = 2,072; bank 11 = 2,691; bank 12 = 2,925; bank 13 = 2,935; bank 14 = 2,609; bank 15 = 3,312; banks 0-9 = zero.

Every accepted identity becomes a fixed `case` in its physical bank source and calls one statically selected opcode-form symbol such as `ai2_v05_op_A9_LDA_Imm`. There are 99 used opcode forms. No generated case fetches a ROM opcode, switches on an opcode byte, decodes a missing instruction or falls back to an interpreter/emulator.

The global dispatcher switches only on physical PRG bank and CPU PC. A missing physical bank returns `AI2_V05_STEP_MISSING_BANK`; a missing PC returns `AI2_V05_STEP_MISSING_IDENTITY`; an unproved indirect/return target returns `AI2_V05_STEP_BAD_DYNAMIC_TARGET`.

Five immutable word-table indirect sites contribute 285 V04-proven entries. The 12 stack-dispatch callsites contribute 119 per-origin inline-table entries. V05 retains those exact origin domains rather than broadening them to an observed union. All 1,133 V04 return continuation identities are generated as the finite RTS continuation validator. RTI remains constrained by the generated-identity dispatcher after mapper-aware PC resolution.

## Bus-order boundary

V05 does not prematurely implement the V06 CPU/bus runtime. Instead each of the 99 fixed opcode-form symbols has a generated ordering contract. Read-modify-write forms explicitly require `data-read -> write-old -> write-new`; indexed stores record their dummy read; branches record taken/page-cross cycles; JSR/RTS/RTI record stack access order; indirect JMP records the 6502 page-wrap high-byte read. V06 must implement these fixed helper contracts exactly.

## Reproducibility

Generator: `ai2-static-generator 0.5.0-v05`; source SHA-256 `294a621438713625377065abf1903d23f003d26324397ea2f0bb12b6b87e3495`. The checked-in generated tree contains 26 files including its self-excluded manifest, with manifest aggregate `9b1950e88ff17b36cd47ed5dd1f599cb720b03739cd277aff61d5c0bdf5bd89e` over 25 hashed files. The pre-receipt content aggregate recorded by the generator is `837b843dd64b39a1f7cdb6d77e45b2e092628f725aef6098be76772c258c6f72`. Two clean output directories were generated and compared byte-for-byte against each other and the checked-in tree.

The generator receipt records normalized arguments as `--rom <EXTERNAL_ROM> --output <OUTPUT> --root <PROJECT_ROOT>` and records hashes for the V04 instruction, edge, indirect, return, closure, opcode and proof inputs plus the generator itself. This preserves reproducibility without leaking a private filesystem ROM path.
