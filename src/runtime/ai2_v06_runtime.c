#include "ai2_v06_runtime.h"

#include <string.h>

static void ai2_v06_set_trap(Ai2V05Runtime *rt, Ai2V06TrapCode code,
                             uint16_t address, uint8_t value, int is_write)
{
    if(rt == NULL || rt->trap.code != AI2_V06_TRAP_NONE) {
        return;
    }
    rt->trap.code = code;
    rt->trap.source_pc = rt->current_pc;
    rt->trap.address = address;
    rt->trap.value = value;
    rt->trap.is_write = is_write ? 1u : 0u;
    rt->trap.physical_bank = 0xFFu;
    rt->trap.cpu_cycle = rt->cpu_cycles;
    rt->trap.ppu_cycle = rt->ppu_cycles;
    rt->trap.apu_cycle = rt->apu_cycles;

    if(address >= 0x8000u) {
        Ai2MapResult mapped = ai2_mmc3_map_cpu_read(&rt->mapper, address);
        if(mapped.kind == AI2_MAP_PRG_ROM) {
            rt->trap.physical_bank = (uint8_t)mapped.physical_bank;
        }
    }
}

static void ai2_v06_observe(Ai2V05Runtime *rt, Ai2V06BusAccessKind kind,
                            uint16_t address, uint8_t value, int is_write)
{
    if(rt->bus_observer != NULL) {
        Ai2V06BusEvent event;
        event.kind = kind;
        event.address = address;
        event.value = value;
        event.is_write = is_write ? 1u : 0u;
        event.cpu_cycle = rt->cpu_cycles;
        rt->bus_observer(rt->bus_observer_user, &event);
    }
}

static void ai2_v06_advance_cycle(Ai2V05Runtime *rt)
{
    unsigned i;
    rt->cpu_cycles++;
    rt->apu_cycles++;
    ai2_mmc3_clock_m2_falling_edge(&rt->mapper);
    if(rt->devices.apu_tick != NULL) {
        rt->devices.apu_tick(rt, rt->devices.user);
    }
    if(rt->devices.dma_tick != NULL) {
        rt->devices.dma_tick(rt, rt->devices.user);
    }
    for(i = 0; i < 3u; i++) {
        rt->ppu_cycles++;
        if(rt->devices.ppu_tick != NULL) {
            rt->devices.ppu_tick(rt, rt->devices.user);
        }
    }

    /* The 2A03 recognizes interrupt state from the prior CPU-cycle poll at an
     * instruction boundary.  Sampling here keeps mapper/APU/PPU-produced lines
     * coherent with the same scheduler that charged the cycle. */
    rt->prev_nmi_poll = rt->nmi_poll;
    if(rt->nmi_pending != 0u) {
        rt->nmi_poll = 1u;
    }
    rt->prev_irq_poll = rt->irq_poll;
    rt->irq_poll = ((rt->external_irq != 0u) ||
        (rt->mapper.irq_pending_known && rt->mapper.irq_pending)) &&
        ((rt->p & AI2_V06_FLAG_I) == 0u) ? 1u : 0u;

    /* V08 DMA arbitration is allowed to steal complete CPU cycles after any
     * ordinary CPU bus cycle.  The guard lets the DMA implementation reuse the
     * normal bus helpers for each stolen read/write without recursively draining. */
    if(rt->dma_draining == 0u && rt->devices.dma_drain != NULL &&
       rt->trap.code == AI2_V06_TRAP_NONE) {
        rt->dma_draining = 1u;
        rt->devices.dma_drain(rt, rt->devices.user);
        rt->dma_draining = 0u;
    }
}

void ai2_v06_internal_cycle(Ai2V05Runtime *rt)
{
    if(rt == NULL || rt->trap.code != AI2_V06_TRAP_NONE) {
        return;
    }
    ai2_v06_observe(rt, AI2_V06_BUS_INTERNAL_CYCLE, rt->current_pc, rt->open_bus, 0);
    ai2_v06_advance_cycle(rt);
}

void ai2_v06_code_cycle(Ai2V05Runtime *rt, uint16_t source_pc, uint8_t static_byte,
                        Ai2V06BusAccessKind kind)
{
    if(rt == NULL || rt->trap.code != AI2_V06_TRAP_NONE) {
        return;
    }
    if(kind == AI2_V06_BUS_CODE_CYCLE) {
        rt->current_pc = source_pc;
    }
    rt->open_bus = static_byte;
    ai2_v06_observe(rt, kind, source_pc, static_byte, 0);
    ai2_v06_advance_cycle(rt);
}

void ai2_v06_init(Ai2V05Runtime *rt, const uint8_t *prg_rom, size_t prg_rom_size,
                  const uint8_t *chr_rom, size_t chr_rom_size, uint8_t ram_fill)
{
    if(rt == NULL) {
        return;
    }
    memset(rt, 0, sizeof(*rt));
    memset(rt->ram, ram_fill, sizeof(rt->ram));
    rt->prg_rom = prg_rom;
    rt->prg_rom_size = prg_rom_size;
    rt->chr_rom = chr_rom;
    rt->chr_rom_size = chr_rom_size;
    rt->s = 0xFDu;
    rt->p = AI2_V06_FLAG_I | AI2_V06_FLAG_U;
    ai2_mmc3_power_on_unknown(&rt->mapper);
    if(prg_rom == NULL || prg_rom_size != AI2_PRG_ROM_BYTES ||
       chr_rom == NULL || chr_rom_size != AI2_CHR_ROM_BYTES) {
        ai2_v06_set_trap(rt, AI2_V06_TRAP_ROM_GEOMETRY, 0u, 0u, 0);
    }
}

void ai2_v06_set_devices(Ai2V05Runtime *rt, const Ai2V06DeviceHooks *hooks)
{
    if(rt == NULL) {
        return;
    }
    if(hooks == NULL) {
        memset(&rt->devices, 0, sizeof(rt->devices));
    } else {
        rt->devices = *hooks;
    }
}

void ai2_v06_set_bus_observer(Ai2V05Runtime *rt, Ai2V06BusObserverFn observer, void *user)
{
    if(rt == NULL) {
        return;
    }
    rt->bus_observer = observer;
    rt->bus_observer_user = user;
}

const Ai2V06Trap *ai2_v06_get_trap(const Ai2V05Runtime *rt)
{
    return rt == NULL ? NULL : &rt->trap;
}

void ai2_v06_clear_trap(Ai2V05Runtime *rt)
{
    if(rt != NULL) {
        memset(&rt->trap, 0, sizeof(rt->trap));
    }
}

int ai2_v06_has_trap(const Ai2V05Runtime *rt)
{
    return rt == NULL || rt->trap.code != AI2_V06_TRAP_NONE;
}

static int ai2_v06_device_read(Ai2V05Runtime *rt, Ai2V06DeviceReadFn fn,
                               Ai2V06TrapCode missing, uint16_t address,
                               Ai2V06BusAccessKind kind, uint8_t *value)
{
    uint8_t v = rt->open_bus;
    if(fn == NULL) {
        ai2_v06_set_trap(rt, missing, address, 0u, 0);
        return -1;
    }
    if(fn(rt, rt->devices.user, address, &v) != 0) {
        ai2_v06_set_trap(rt, AI2_V06_TRAP_DEVICE_REJECTED, address, v, 0);
        return -1;
    }
    rt->open_bus = v;
    ai2_v06_observe(rt, kind, address, v, 0);
    ai2_v06_advance_cycle(rt);
    *value = v;
    return 0;
}

static int ai2_v06_device_write(Ai2V05Runtime *rt, Ai2V06DeviceWriteFn fn,
                                Ai2V06TrapCode missing, uint16_t address, uint8_t value,
                                Ai2V06BusAccessKind kind)
{
    if(fn == NULL) {
        ai2_v06_set_trap(rt, missing, address, value, 1);
        return -1;
    }
    if(fn(rt, rt->devices.user, address, value) != 0) {
        ai2_v06_set_trap(rt, AI2_V06_TRAP_DEVICE_REJECTED, address, value, 1);
        return -1;
    }
    rt->open_bus = value;
    ai2_v06_observe(rt, kind, address, value, 1);
    ai2_v06_advance_cycle(rt);
    return 0;
}

int ai2_v06_bus_read(Ai2V05Runtime *rt, uint16_t address, Ai2V06BusAccessKind kind, uint8_t *value)
{
    uint8_t v;
    if(rt == NULL || value == NULL) {
        return -1;
    }
    if(rt->trap.code != AI2_V06_TRAP_NONE) {
        return -1;
    }

    if(address < 0x2000u) {
        v = rt->ram[address & 0x07FFu];
    } else if(address < 0x4000u) {
        uint16_t reg = (uint16_t)(0x2000u | (address & 7u));
        return ai2_v06_device_read(rt, rt->devices.ppu_read,
                                   AI2_V06_TRAP_PPU_NOT_CONSTRUCTED, reg, kind, value);
    } else if(address <= 0x4017u) {
        return ai2_v06_device_read(rt, rt->devices.apu_io_read,
                                   AI2_V06_TRAP_APU_IO_NOT_CONSTRUCTED, address, kind, value);
    } else if(address <= 0x401Fu) {
        v = rt->open_bus;
    } else {
        Ai2MapResult mapped = ai2_mmc3_map_cpu_read(&rt->mapper, address);
        if(mapped.kind == AI2_MAP_OPEN_BUS) {
            v = rt->open_bus;
        } else if(mapped.kind == AI2_MAP_PRG_ROM) {
            if(mapped.segment_offset >= rt->prg_rom_size) {
                ai2_v06_set_trap(rt, AI2_V06_TRAP_BAD_PRG_MAPPING, address, 0u, 0);
                return -1;
            }
            v = rt->prg_rom[mapped.segment_offset];
        } else if(mapped.kind == AI2_MAP_UNRESOLVED_POWER_STATE) {
            ai2_v06_set_trap(rt, AI2_V06_TRAP_UNRESOLVED_MAPPER, address, 0u, 0);
            return -1;
        } else {
            ai2_v06_set_trap(rt, AI2_V06_TRAP_BAD_PRG_MAPPING, address, 0u, 0);
            return -1;
        }
    }

    rt->open_bus = v;
    ai2_v06_observe(rt, kind, address, v, 0);
    ai2_v06_advance_cycle(rt);
    *value = v;
    return 0;
}

int ai2_v06_bus_write(Ai2V05Runtime *rt, uint16_t address, uint8_t value, Ai2V06BusAccessKind kind)
{
    if(rt == NULL || rt->trap.code != AI2_V06_TRAP_NONE) {
        return -1;
    }

    if(address < 0x2000u) {
        rt->ram[address & 0x07FFu] = value;
    } else if(address < 0x4000u) {
        uint16_t reg = (uint16_t)(0x2000u | (address & 7u));
        return ai2_v06_device_write(rt, rt->devices.ppu_write,
                                    AI2_V06_TRAP_PPU_NOT_CONSTRUCTED, reg, value, kind);
    } else if(address == 0x4014u) {
        return ai2_v06_device_write(rt, rt->devices.oam_dma_start,
                                    AI2_V06_TRAP_DMA_NOT_CONSTRUCTED, address, value, kind);
    } else if(address <= 0x4017u) {
        return ai2_v06_device_write(rt, rt->devices.apu_io_write,
                                    AI2_V06_TRAP_APU_IO_NOT_CONSTRUCTED, address, value, kind);
    } else if(address <= 0x401Fu) {
        /* Disabled CPU test-mode registers: write has no device effect. */
    } else if(address >= 0x8000u) {
        Ai2MapperWriteResult wr = ai2_mmc3_cpu_write(&rt->mapper, address, value);
        if(wr == AI2_MAPPER_WRITE_AMBIGUOUS_POWER_STATE) {
            ai2_v06_set_trap(rt, AI2_V06_TRAP_UNRESOLVED_MAPPER, address, value, 1);
            return -1;
        }
        if(wr != AI2_MAPPER_WRITE_APPLIED) {
            ai2_v06_set_trap(rt, AI2_V06_TRAP_BAD_PRG_MAPPING, address, value, 1);
            return -1;
        }
    } else {
        /* $4020-$7FFF is unpopulated/open-bus cartridge space on TLROM-03. */
    }

    rt->open_bus = value;
    ai2_v06_observe(rt, kind, address, value, 1);
    ai2_v06_advance_cycle(rt);
    return 0;
}

int ai2_v05_runtime_map_prg_identity(Ai2V05Runtime *rt, uint16_t pc, uint8_t *physical_bank)
{
    Ai2MapResult mapped;
    if(rt == NULL || physical_bank == NULL) {
        return -1;
    }
    mapped = ai2_mmc3_map_cpu_read(&rt->mapper, pc);
    if(mapped.kind == AI2_MAP_UNRESOLVED_POWER_STATE) {
        ai2_v06_set_trap(rt, AI2_V06_TRAP_UNRESOLVED_MAPPER, pc, 0u, 0);
        return -1;
    }
    if(mapped.kind != AI2_MAP_PRG_ROM || mapped.physical_bank >= AI2_PRG_BANK_COUNT) {
        ai2_v06_set_trap(rt, AI2_V06_TRAP_BAD_PRG_MAPPING, pc, 0u, 0);
        return -1;
    }
    *physical_bank = (uint8_t)mapped.physical_bank;
    return 0;
}

int ai2_v05_runtime_note_stack_dispatch_origin(Ai2V05Runtime *rt, uint8_t bank, uint16_t pc)
{
    if(rt == NULL) {
        return -1;
    }
    if(rt->stack_dispatch_depth >= AI2_V06_STACK_DISPATCH_DEPTH) {
        ai2_v06_set_trap(rt, AI2_V06_TRAP_STACK_DISPATCH_OVERFLOW, pc, bank, 0);
        return -1;
    }
    rt->stack_dispatch[rt->stack_dispatch_depth].bank = bank;
    rt->stack_dispatch[rt->stack_dispatch_depth].pc = pc;
    rt->stack_dispatch_depth++;
    return 0;
}

int ai2_v05_runtime_get_stack_dispatch_origin(Ai2V05Runtime *rt, uint8_t *bank, uint16_t *pc)
{
    if(rt == NULL || bank == NULL || pc == NULL || rt->stack_dispatch_depth == 0u) {
        if(rt != NULL) {
            ai2_v06_set_trap(rt, AI2_V06_TRAP_STACK_DISPATCH_UNDERFLOW, rt->current_pc, 0u, 0);
        }
        return -1;
    }
    *bank = rt->stack_dispatch[rt->stack_dispatch_depth - 1u].bank;
    *pc = rt->stack_dispatch[rt->stack_dispatch_depth - 1u].pc;
    return 0;
}

void ai2_v05_runtime_clear_stack_dispatch_origin(Ai2V05Runtime *rt)
{
    if(rt != NULL && rt->stack_dispatch_depth != 0u) {
        rt->stack_dispatch_depth--;
    }
}

void ai2_v06_request_nmi(Ai2V05Runtime *rt)
{
    if(rt != NULL) {
        rt->nmi_pending = 1u;
    }
}

void ai2_v06_clear_nmi(Ai2V05Runtime *rt)
{
    if(rt != NULL) {
        rt->nmi_pending = 0u;
        rt->nmi_poll = 0u;
        rt->prev_nmi_poll = 0u;
    }
}

void ai2_v06_raise_device_trap(Ai2V05Runtime *rt, Ai2V06TrapCode code, uint16_t address, uint8_t value, int is_write)
{
    ai2_v06_set_trap(rt, code, address, value, is_write);
}

void ai2_v06_set_external_irq(Ai2V05Runtime *rt, int asserted)
{
    if(rt != NULL) {
        rt->external_irq = asserted ? 1u : 0u;
    }
}

static int ai2_v06_push(Ai2V05Runtime *rt, uint8_t value)
{
    uint16_t address = (uint16_t)(0x0100u | rt->s);
    if(ai2_v06_bus_write(rt, address, value, AI2_V06_BUS_STACK_WRITE) != 0) {
        return -1;
    }
    rt->s--;
    return 0;
}

static int ai2_v06_interrupt(Ai2V05Runtime *rt, uint16_t pc, uint16_t vector, Ai2V05Identity *next)
{
    uint8_t lo, hi, bank, ignored;
    if(ai2_v06_bus_read(rt, pc, AI2_V06_BUS_DUMMY_READ, &ignored) != 0) return -1;
    if(ai2_v06_bus_read(rt, pc, AI2_V06_BUS_DUMMY_READ, &ignored) != 0) return -1;
    if(ai2_v06_push(rt, (uint8_t)(pc >> 8)) != 0) return -1;
    if(ai2_v06_push(rt, (uint8_t)pc) != 0) return -1;
    if(ai2_v06_push(rt, (uint8_t)((rt->p & (uint8_t)~AI2_V06_FLAG_B) | AI2_V06_FLAG_U)) != 0) return -1;
    rt->p = (uint8_t)((rt->p | AI2_V06_FLAG_I | AI2_V06_FLAG_U) & (uint8_t)~AI2_V06_FLAG_B);
    if(ai2_v06_bus_read(rt, vector, AI2_V06_BUS_VECTOR_READ, &lo) != 0) return -1;
    if(ai2_v06_bus_read(rt, (uint16_t)(vector + 1u), AI2_V06_BUS_VECTOR_READ, &hi) != 0) return -1;
    next->pc = (uint16_t)(lo | ((uint16_t)hi << 8));
    if(ai2_v05_runtime_map_prg_identity(rt, next->pc, &bank) != 0 ||
       !ai2_v05_generated_identity_exists(bank, next->pc)) {
        ai2_v06_set_trap(rt, AI2_V06_TRAP_BAD_INTERRUPT_TARGET, next->pc, 0u, 0);
        return -1;
    }
    next->bank = bank;
    return 0;
}

int ai2_v06_reset(Ai2V05Runtime *rt, Ai2V05Identity *entry)
{
    uint8_t lo, hi, bank;
    unsigned i;
    if(rt == NULL || entry == NULL || rt->trap.code != AI2_V06_TRAP_NONE) {
        return -1;
    }
    ai2_mmc3_cpu_reset(&rt->mapper);
    rt->a = rt->x = rt->y = 0u;
    rt->s = 0xFDu;
    rt->p = AI2_V06_FLAG_I | AI2_V06_FLAG_U;
    rt->nmi_pending = 0u;
    rt->nmi_poll = 0u;
    rt->prev_nmi_poll = 0u;
    rt->external_irq = 0u;
    rt->irq_poll = 0u;
    rt->prev_irq_poll = 0u;
    rt->stack_dispatch_depth = 0u;
    rt->dma_draining = 0u;
    if(rt->devices.reset != NULL) {
        rt->devices.reset(rt, rt->devices.user, 1);
        if(rt->trap.code != AI2_V06_TRAP_NONE) return -1;
    }
    rt->current_pc = 0xFFFCu;
    for(i = 0; i < 5u; i++) {
        ai2_v06_internal_cycle(rt);
    }
    if(ai2_v06_bus_read(rt, 0xFFFCu, AI2_V06_BUS_VECTOR_READ, &lo) != 0) return -1;
    if(ai2_v06_bus_read(rt, 0xFFFDu, AI2_V06_BUS_VECTOR_READ, &hi) != 0) return -1;
    entry->pc = (uint16_t)(lo | ((uint16_t)hi << 8));
    if(ai2_v05_runtime_map_prg_identity(rt, entry->pc, &bank) != 0 ||
       !ai2_v05_generated_identity_exists(bank, entry->pc)) {
        ai2_v06_set_trap(rt, AI2_V06_TRAP_BAD_INTERRUPT_TARGET, entry->pc, 0u, 0);
        return -1;
    }
    entry->bank = bank;
    rt->current_pc = entry->pc;
    return 0;
}

int ai2_v06_step(Ai2V05Runtime *rt, Ai2V05Identity current, Ai2V05Identity *next)
{
    uint8_t mapped_bank;
    Ai2V05StepResult step;
    Ai2V05Identity continuation;
    if(rt == NULL || next == NULL || rt->trap.code != AI2_V06_TRAP_NONE) {
        return -1;
    }
    rt->current_pc = current.pc;
    if(ai2_v05_runtime_map_prg_identity(rt, current.pc, &mapped_bank) != 0 || mapped_bank != current.bank ||
       !ai2_v05_generated_identity_exists(current.bank, current.pc)) {
        ai2_v06_set_trap(rt, AI2_V06_TRAP_BAD_GENERATED_IDENTITY, current.pc, current.bank, 0);
        return -1;
    }

    step = ai2_v05_dispatch(rt, current, &continuation);
    if(step != AI2_V05_STEP_OK) {
        ai2_v06_set_trap(rt, AI2_V06_TRAP_GENERATED_DISPATCH, current.pc, (uint8_t)step, 0);
        return -1;
    }
    if(rt->trap.code != AI2_V06_TRAP_NONE) {
        return -1;
    }

    /* Interrupt recognition is an instruction-boundary result of cycle polling,
     * not a pre-dispatch shortcut. NMI has priority over maskable IRQ. */
    if(rt->prev_nmi_poll != 0u) {
        rt->nmi_pending = 0u;
        rt->nmi_poll = 0u;
        rt->prev_nmi_poll = 0u;
        return ai2_v06_interrupt(rt, continuation.pc, 0xFFFAu, next);
    }
    if(rt->prev_irq_poll != 0u) {
        return ai2_v06_interrupt(rt, continuation.pc, 0xFFFEu, next);
    }

    *next = continuation;
    return 0;
}
