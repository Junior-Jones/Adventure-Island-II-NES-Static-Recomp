# V04 control-flow and executable-RAM closure

## Authority and method

Version 04 is constructed only from the exact Adventure Island II ROM bytes, the V02
TLROM-03/Sharp MMC3B mapping contract, the V03 physical PRG inventory/opcode metadata,
and reviewed finite static proofs in `config/v04-static-proofs.json`. No gameplay run,
trace, observed PC list, save state, screenshot, controller route, or emulator execution
coverage is an admission source.

Roots are RESET `$FFAE`, NMI `$E0CE`, and IRQ `$E152` in fixed physical PRG bank 15.
Every accepted instruction is identified by physical 8 KiB PRG bank plus CPU PC.

## Current static graph

The deterministic current graph contains 16,544 accepted instruction contexts occupying
35,423 immutable PRG bytes. Per physical 8 KiB bank: bank 10 has 2,072 instructions /
4,410 bytes; bank 11 has 2,691 / 5,823; bank 12 has 2,925 / 6,748; bank 13 has 2,935 /
6,204; bank 14 has 2,609 / 5,350; and bank 15 has 3,312 / 6,888.

The generated edge ledger contains 19,833 normalized records in the rebuilt V04 format.
Every edge which names a ROM target names an accepted instruction start. There are zero
unresolved current-graph edges, zero instruction/inline-data overlaps, and 1,133 explicit
return-continuation records.

The earlier transient V04 work reported 19,950 edge records before the working tree was
lost. That transient edge count is not retained as authority. Reconstructing the graph
from V03 and correcting the bank-10 dispatcher table reproduces the material authority
counts (16,544 instructions, 35,423 bytes, 1,133 continuations, 404 indirect targets);
the checked-in normalized edge ledger is now the reproducible V04 edge authority.

## Finite indirect control flow

Exactly 404 indirect target entries are statically admitted. Five normal indirect
word-table dispatchers contribute 285 entries: `$F27C` has 13, `$F2F3` has 22,
physical-bank-10 `$ABFB` has 26, physical-bank-11 `$A3E6` has 96, and physical-bank-12
`$A1BB` has 128. The bank-12 dispatcher is exhaustive for every possible input byte
because the 8-bit `ASL` index can produce only the 128 even offsets `$00..$FE`.

The `$EDA3` stack dispatcher, reached directly or through `$FF84`, contributes another
119 inline word-table entries across 12 statically discovered call sites. It pops the JSR
return address and uses the bytes immediately following the JSR as the dispatch table;
those bytes are therefore data and are excluded from the instruction ledger.

`$EDC1/$EDE2`, reached through `$FF9C`, is a separate return-address transformer. The
accepted physical-bank-11 call at `$B9E3` owns six inline parameter bytes `$B9E6-$B9EB`
and resumes at statically proven continuation `$B9EC`, rather than decoding the inline
parameters as instructions.

## MMC3 executable bank-value closure

All accepted transfers into the `$A000-$BFFF` switchable PRG window are tied to a finite
MMC3 R7 proof. The executable R7 set is exactly `{10, 11, 12, 13}`. In particular,
fixed-bank code at `$DFE4` executes `LDX #$07`, `LDA #$0C`, `JSR $FF6F` and then
`JMP $A006`, proving physical PRG bank 12 executable without gameplay discovery.
The other transfer epochs are enumerated in `config/v04-static-proofs.json` and every
declared transfer is independently required by the V04 construction test to exist in
the generated graph.

No accepted transfer into `$8000-$9FFF` remains unproved. R6 is therefore not silently
used as an executable-bank allowlist in V04.

## Executable RAM / mutation closure

The current graph has zero control-flow targets below `$8000`, zero executable-RAM
epochs, zero statically proven ROM-to-RAM copies used as executable code, and zero
self-modifying current-graph code. Accepted instructions reside in immutable cartridge
PRG ROM; writes in cartridge CPU space are mapper/bus operations, not mutation of those
instruction bytes.

This is deliberately a current-graph statement, not a whole-ROM impossibility claim.
Any future statically proven RAM execution must define an explicit epoch, byte signature,
entry point and invalidation rule before it can be admitted; otherwise it fails closed.

## Separate closure statements

Current-graph closure: COMPLETE for V04. Every accepted instruction edge resolves to a
statically proven target/semantic return relation and the unresolved-edge ledger is empty.

ROM-wide closure: NOT CLAIMED by V04. PRG bytes outside the accepted current graph remain
subject to later whole-ROM closure. Their absence from this graph is not treated as proof
that they can never execute.
