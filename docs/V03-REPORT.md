# Adventure Island II NES Static Recomp - Version 03 report

Result: **complete for the full Starter NES Revision 3 V03 scope**.

V03 inventories all sixteen 8 KiB physical PRG banks and exactly 131,072 PRG bytes.
Every byte starts as unclassified data. RESET/NMI/IRQ are rooted through the V02
fixed-bank mapping, producing a deterministic ten-instruction ledger before the first
control-transfer boundary on each root path. No direct branch, call, jump, return or
indirect edge is traversed in V03 because that is the V04 milestone.

The primary inventory contains 16 code bytes, 6 vector bytes, 16 proven title-text
bytes, and 131,034 unclassified bytes. Zero bytes are called padding. The architectural
vector table and three vector pointers are separately classified. No graphics
reference is guessed without a proven use-site.

Validation: V03 inventory construction tests pass 563 checks; two clean generations
are byte-identical; all 256 decoder metadata entries match the pinned MesenCE 2.2.1
source; altered ROM input is rejected; decode-conflict and overlap ledgers are empty;
V01 validation regresses cleanly; the V02 mapper suite still passes 1,869,243 checks;
the MesenCE headless oracle self-test passes; the V05 generator still refuses early
execution; and no `.nes` image exists inside the project tree.

Open boundaries are exactly the four V04 seeds emitted by V03: RESET JMP to $E010,
NMI branch-taken to $E0E1, NMI branch-fallthrough to $E0DA, and the IRQ RTI return
whose destination is stack/context dependent.

Next milestone: V04 control-flow and executable-RAM closure. V04 must recursively
follow direct branches/JSR/JMP/interrupt/return edges, prove finite mapper-bank and
indirect-target sets, identify self-modification or ROM-to-RAM executable copies,
define any RAM-code epochs/signatures/invalidation rules, and leave every unresolved
edge explicit rather than using a trace-only allowlist.
