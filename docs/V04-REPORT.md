# Adventure Island II NES Static Recomp - Version 04 report

Version 04 fully completes the Starter NES V04 control-flow and executable-RAM closure
scope. Construction is offline and static only. No gameplay route or execution trace was
used to discover, admit, or prove target code.

The exact-ROM graph closes at 16,544 physical-bank instruction contexts and 35,423 PRG
instruction bytes. It contains 1,133 return continuations and 404 finite indirect target
entries, with zero unresolved current-graph edges and zero instruction/inline-data
overlaps. The rebuilt normalized edge ledger contains 19,833 records.

Five immutable indirect dispatchers contribute 13, 22, 26, 96 and 128 entries. The
`$EDA3/$FF84` stack-dispatch mechanism contributes 119 inline entries across 12 callsites.
The `$EDC1/$EDE2/$FF9C` return-adjust mechanism consumes six inline bytes at the one
accepted bank-11 callsite and resumes at `$B9EC`.

MMC3 R7 executable values are statically closed to physical banks 10, 11, 12 and 13.
Bank 12 is proven by fixed-bank instructions `$DFE4-$DFED`, which explicitly program
R7 to `$0C` before transferring to `$A006`.

The current graph has no executable RAM target, executable RAM epoch, ROM-to-RAM copy
used as code, or self-modifying code. V04 does not claim ROM-wide exclusion of future
code outside the accepted graph; that statement remains deliberately separate for later
whole-ROM closure.

The independent V04 construction suite passes 272,518 checks. It verifies immutable ROM
bytes, every explicit target, all indirect entries, return/stack transformations, mapper
bank transfers, PRG occupancy, clean deterministic regeneration, negative ROM identity,
ROM exclusion, and the V05 production-generator lock. V01, V02 and V03 regressions and
the pinned MesenCE oracle self-test also pass.

V05 production lowering remains prohibited until this sealed V04 is used as its sole
accepted instruction/target authority.
