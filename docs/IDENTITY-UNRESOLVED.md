# Identity, hardware and program unresolved facts through V03

The legacy iNES header has no NES 2.0 submapper field. `submapper` remains `null`;
no file-format value is invented. Exact matching physical cartridge evidence selects
NES-TLROM-03 with a Sharp MMC3B, so this is not a target-hardware blocker.

Cold-power values of switchable MMC3 latches are not guaranteed deterministic. V02
represents those latches as explicit unknown state and fails closed whenever an
operation depends on state software has not established. This is intentional hardware
modeling, not a missing V02 requirement.

V03 has no decode or overlap conflicts and no target-hardware blockers. It intentionally
opens four V04 control-flow boundaries: RESET's direct JMP, both outcomes of the first
NMI conditional branch, and IRQ's RTI return. They are recorded in the machine-readable
unresolved ledger and are not grounds for pretending V03 is incomplete, because V04
owns direct/indirect/return control-flow closure.

The remaining 131,034 PRG bytes are explicitly unclassified, not padding. Their code,
table, pointer, graphics-reference or other role can only be promoted by later static
proof. No gameplay trace has been used to classify them.
