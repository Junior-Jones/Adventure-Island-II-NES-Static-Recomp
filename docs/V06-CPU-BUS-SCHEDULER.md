# V06 CPU, bus, scheduler and mapper runtime

## Authority and construction rule

V06 is built under `Starter NES(4).zip`. Static construction uses the exact ROM, sealed V04 graph and V05 fixed generated identities. It does not use a title/menu/gameplay route, controller sequence, execution trace, snapshot, screenshot or oracle run to admit code. MesenCE is an independent reference and self-test oracle only at this milestone.

## CPU authority

The V05 graph requires 99 distinct 2A03 opcode forms across 16,544 admitted instruction identities. V06 implements all 99 fixed semantic entry points. The admitted set contains zero unofficial forms and zero BRK contexts. V06 therefore does not create speculative general BRK or unofficial-opcode execution lanes: an unproved physical-bank/PC identity fails before a semantic function is selected.

The implemented target-required semantics include binary NES ADC/SBC with decimal arithmetic disabled, flags, zero-page wrapping, indexed page-cross cycles, branch cycles, JMP-indirect page wrap, stack wrapping, PHP/PHA/PLP/PLA, JSR/RTS/RTI, accumulator shifts/rotates, read-modify-write old/new bus order, stores and all V04-required ALU/load/compare/transfer forms. Implied, accumulator, stack and return operations perform their discarded/dummy reads rather than substituting anonymous timing cycles.

NMI and maskable IRQ state is sampled by the common CPU-cycle scheduler and recognized at an instruction boundary after the statically generated instruction. NMI has priority. External and Sharp MMC3B IRQ sources feed the same maskable-IRQ poll.

## Bus authority

Every runtime data/vector/stack/dummy access uses the V06 bus. Internal RAM mirrors through `$1FFF`. PPU mirrors resolve to `$2000-$2007` and require a V07 device implementation. `$4000-$4017` is routed to the future V08 APU/controller layer, with `$4014` separated as the OAM-DMA start boundary. `$4018-$401F` has CPU test-mode/open-bus behavior. The TLROM-03 `$4020-$7FFF` region has no PRG RAM and remains open/unpopulated. Mapper writes and `$8000-$FFFF` data/vector reads use the same V02 Sharp MMC3B object that maps generated physical-bank identities.

Missing PPU, APU/controller or DMA semantics never return a convenient value. They generate a structured trap containing PC, address, value/direction and scheduler counters. Bad mapper state, bad PRG mapping, missing generated identity and bad interrupt targets also fail closed.

## Scheduler authority

One CPU-cycle function advances one CPU tick, one APU tick, one DMA tick, three PPU ticks and one mapper M2-falling-edge observation. PPU/APU/DMA callbacks are later-device construction boundaries, not second timing loops. The frontend does not own emulated timing.

## Static reset frontier

A deterministic frame-zero, no-input construction probe follows only already generated authority. It resolves RESET `$FFAE` and completes four generated instructions: `$FFAE JMP $E010`, `$E010 SEI`, `$E011 CLD`, `$E012 LDX #$00`. The next instruction `$E014 STX $2000` reaches the first V07-owned PPU write and fails closed with `PPU_NOT_CONSTRUCTED`. The trap occurs at CPU cycle 19, PPU cycle 57 and APU cycle 19. Two independent runs produce identical JSON.

This is a construction frontier only. It does not claim title-screen, gameplay or cold-boot closure.

## V06 closure statement

V06 closes its defined CPU/bus/common-scheduler/mapper-runtime scope. The current accepted CPU graph remains the V04 authority and ROM-wide fixed-point closure remains V10 work. Machine closure remains incomplete because V07 PPU and V08 APU/DMA/input semantics have not yet been constructed. Qualification/gameplay remains forbidden before V10.
