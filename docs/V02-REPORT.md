# Version 02 completion report

Version 02 is complete for its Starter NES scope: the exact Adventure Island II board
and mapper contract is selected, documented, implemented as a single mapping
authority, and construction-tested.

The target is Nintendo `NES-TLROM-03` using a Sharp MMC3B. The contract defines cold
power unknowns without guessed defaults, CPU reset preservation, all PRG and CHR
windows, physical bank numbering, complete `$8000/$8001/$A000/$A001/$C000/$C001/
$E000/$E001` write decoding and state transitions, mapper-controlled nametable
mirroring, the exact absence of PRG/CHR RAM, no bus conflicts, no expansion audio,
and Sharp/new MMC3B A12-qualified IRQ behavior.

The final V02 C construction suite passes 1,869,243 checks with the exact supplied ROM
used only for vector integration. It exhaustively covers CPU write decoding, complete
CPU/PPU read address spaces in an established state, PRG/CHR mapping modes and bank
normalization, nametable mapping, register transitions, reset preservation, and every
latch/counter/reload/enable combination of the Sharp IRQ counter. The machine-readable
contract validator also passes. V01 exact-image and negative-image regression tests
pass, and the MesenCE NES-only oracle self-test passes.

No gameplay test was run and no gameplay-driven discovery was used. The Starter NES
rules reserve gameplay verification until the static core is complete at V10. V03 and
V05 source-pinned tool gates remain deliberately closed at this V02 completion point.

There are no unresolved V02 target-hardware blockers. The lack of an encoded NES 2.0
submapper remains recorded as a file-format fact; exact physical board evidence is the
selection authority for this cartridge and prevents a guessed submapper value.

Next milestone: V03, ROM-wide disassembly inventory. It must initially treat every
physical PRG byte as data, seed code only from proven roots such as the fixed-bank
vectors, preserve physical-bank identities under this V02 mapper contract, classify
code/data deterministically, and produce the required inventory without using gameplay
as a discovery mechanism.
