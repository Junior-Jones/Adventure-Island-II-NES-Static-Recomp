#include "ai2_mmc3_mapper.h"

#include <string.h>

static Ai2MapResult map_result(Ai2MapKind kind)
{
    Ai2MapResult result;
    memset(&result, 0, sizeof(result));
    result.kind = kind;
    return result;
}

static bool bank_reg_known(const Ai2Mmc3State *state, unsigned reg)
{
    return (state->bank_reg_known_mask & (uint8_t)(1u << reg)) != 0;
}

static Ai2MapResult map_prg(uint8_t bank, uint16_t address)
{
    Ai2MapResult result = map_result(AI2_MAP_PRG_ROM);
    result.physical_bank = bank;
    result.bank_offset = (uint16_t)(address & 0x1FFFu);
    result.segment_offset = (uint32_t)bank * AI2_PRG_BANK_BYTES + result.bank_offset;
    return result;
}

static Ai2MapResult map_chr(uint8_t bank, uint16_t address)
{
    Ai2MapResult result = map_result(AI2_MAP_CHR_ROM);
    result.physical_bank = bank;
    result.bank_offset = (uint16_t)(address & 0x03FFu);
    result.segment_offset = (uint32_t)bank * AI2_CHR_BANK_BYTES + result.bank_offset;
    return result;
}

uint8_t ai2_mmc3_physical_prg_bank(uint8_t mmc3_prg_value)
{
    /* MMC3 R6/R7 expose 6 PRG address bits; this TLROM wires only four. */
    return (uint8_t)((mmc3_prg_value & 0x3Fu) & (AI2_PRG_BANK_COUNT - 1));
}

uint8_t ai2_mmc3_physical_chr_bank(uint8_t mmc3_chr_value)
{
    /* This 128 KiB CHR ROM uses seven 1 KiB bank address bits. */
    return (uint8_t)(mmc3_chr_value & (AI2_CHR_BANK_COUNT - 1));
}

void ai2_mmc3_power_on_unknown(Ai2Mmc3State *state)
{
    memset(state, 0, sizeof(*state));
    /* All switchable mapper latches intentionally remain marked unknown. */
}

void ai2_mmc3_cpu_reset(Ai2Mmc3State *state)
{
    (void)state;
    /* The NES cartridge connector/MMC3 has no dedicated CPU /RESET input. */
}

Ai2MapResult ai2_mmc3_map_cpu_read(const Ai2Mmc3State *state, uint16_t address)
{
    if(address < 0x4020u) {
        return map_result(AI2_MAP_NOT_CARTRIDGE);
    }
    if(address < 0x8000u) {
        /* TLROM-03 for this cartridge has no PRG RAM or expansion hardware. */
        return map_result(AI2_MAP_OPEN_BUS);
    }

    if(address >= 0xE000u) {
        return map_prg(AI2_FIXED_PRG_LAST, address);
    }

    if(address >= 0xA000u && address < 0xC000u) {
        if(!bank_reg_known(state, 7)) {
            return map_result(AI2_MAP_UNRESOLVED_POWER_STATE);
        }
        return map_prg(ai2_mmc3_physical_prg_bank(state->bank_regs[7]), address);
    }

    if(!state->bank_select_known) {
        return map_result(AI2_MAP_UNRESOLVED_POWER_STATE);
    }

    const bool prg_mode = (state->bank_select & 0x40u) != 0;
    if(address < 0xA000u) {
        if(prg_mode) {
            return map_prg(AI2_FIXED_PRG_SECOND_LAST, address);
        }
        if(!bank_reg_known(state, 6)) {
            return map_result(AI2_MAP_UNRESOLVED_POWER_STATE);
        }
        return map_prg(ai2_mmc3_physical_prg_bank(state->bank_regs[6]), address);
    }

    /* $C000-$DFFF */
    if(!prg_mode) {
        return map_prg(AI2_FIXED_PRG_SECOND_LAST, address);
    }
    if(!bank_reg_known(state, 6)) {
        return map_result(AI2_MAP_UNRESOLVED_POWER_STATE);
    }
    return map_prg(ai2_mmc3_physical_prg_bank(state->bank_regs[6]), address);
}

static bool chr_slot_to_register(const Ai2Mmc3State *state, unsigned slot,
                                 unsigned *reg_out, unsigned *pair_offset_out)
{
    if(!state->bank_select_known) {
        return false;
    }

    const bool chr_invert = (state->bank_select & 0x80u) != 0;
    unsigned reg = 0;
    unsigned pair_offset = 0;

    if(!chr_invert) {
        static const uint8_t regs[8] = {0, 0, 1, 1, 2, 3, 4, 5};
        reg = regs[slot];
        pair_offset = (slot == 1u || slot == 3u) ? 1u : 0u;
    } else {
        static const uint8_t regs[8] = {2, 3, 4, 5, 0, 0, 1, 1};
        reg = regs[slot];
        pair_offset = (slot == 5u || slot == 7u) ? 1u : 0u;
    }

    *reg_out = reg;
    *pair_offset_out = pair_offset;
    return true;
}

Ai2MapResult ai2_mmc3_map_ppu_read(const Ai2Mmc3State *state, uint16_t address)
{
    address &= 0x3FFFu;

    if(address < 0x2000u) {
        const unsigned slot = address >> 10;
        unsigned reg = 0;
        unsigned pair_offset = 0;
        if(!chr_slot_to_register(state, slot, &reg, &pair_offset) ||
           !bank_reg_known(state, reg)) {
            return map_result(AI2_MAP_UNRESOLVED_POWER_STATE);
        }

        uint8_t value = state->bank_regs[reg];
        if(reg <= 1u) {
            value = (uint8_t)((value & 0xFEu) | pair_offset);
        }
        return map_chr(ai2_mmc3_physical_chr_bank(value), address);
    }

    if(address < 0x3F00u) {
        if(!state->mirroring_known) {
            return map_result(AI2_MAP_UNRESOLVED_POWER_STATE);
        }

        /* $3000-$3EFF mirrors $2000-$2EFF. */
        if(address >= 0x3000u) {
            address = (uint16_t)(address - 0x1000u);
        }
        const uint16_t nt_offset = (uint16_t)(address - 0x2000u);
        const uint8_t table = (uint8_t)((nt_offset >> 10) & 3u);
        Ai2MapResult result = map_result(AI2_MAP_CIRAM);
        result.ciram_page = state->mirroring == AI2_MIRROR_VERTICAL
            ? (uint8_t)(table & 1u)
            : (uint8_t)((table >> 1) & 1u);
        result.bank_offset = (uint16_t)(nt_offset & 0x03FFu);
        result.segment_offset = (uint32_t)result.ciram_page * 0x400u + result.bank_offset;
        return result;
    }

    /* Palette RAM is internal to the PPU, not cartridge-owned. */
    return map_result(AI2_MAP_NOT_CARTRIDGE);
}

Ai2Mmc3Register ai2_mmc3_decode_cpu_write(uint16_t address)
{
    if(address < 0x8000u) {
        return AI2_MMC3_REG_NONE;
    }
    switch(address & 0xE001u) {
    case 0x8000u: return AI2_MMC3_REG_BANK_SELECT;
    case 0x8001u: return AI2_MMC3_REG_BANK_DATA;
    case 0xA000u: return AI2_MMC3_REG_MIRRORING;
    case 0xA001u: return AI2_MMC3_REG_RAM_PROTECT;
    case 0xC000u: return AI2_MMC3_REG_IRQ_LATCH;
    case 0xC001u: return AI2_MMC3_REG_IRQ_RELOAD;
    case 0xE000u: return AI2_MMC3_REG_IRQ_DISABLE;
    case 0xE001u: return AI2_MMC3_REG_IRQ_ENABLE;
    default: return AI2_MMC3_REG_NONE;
    }
}

Ai2MapperWriteResult ai2_mmc3_cpu_write(Ai2Mmc3State *state, uint16_t address, uint8_t value)
{
    switch(ai2_mmc3_decode_cpu_write(address)) {
    case AI2_MMC3_REG_BANK_SELECT:
        state->bank_select = value;
        state->bank_select_known = true;
        return AI2_MAPPER_WRITE_APPLIED;

    case AI2_MMC3_REG_BANK_DATA: {
        if(!state->bank_select_known) {
            return AI2_MAPPER_WRITE_AMBIGUOUS_POWER_STATE;
        }
        const unsigned reg = state->bank_select & 7u;
        if(reg <= 1u) {
            value &= 0xFEu;
        } else if(reg >= 6u) {
            value &= 0x3Fu;
        }
        state->bank_regs[reg] = value;
        state->bank_reg_known_mask |= (uint8_t)(1u << reg);
        return AI2_MAPPER_WRITE_APPLIED;
    }

    case AI2_MMC3_REG_MIRRORING:
        state->mirroring = (value & 1u) ? AI2_MIRROR_HORIZONTAL : AI2_MIRROR_VERTICAL;
        state->mirroring_known = true;
        return AI2_MAPPER_WRITE_APPLIED;

    case AI2_MMC3_REG_RAM_PROTECT:
        state->ram_control = (uint8_t)(value & 0xC0u);
        state->ram_control_known = true;
        /* No RAM chip is populated on the exact TLROM-03 board. */
        return AI2_MAPPER_WRITE_APPLIED;

    case AI2_MMC3_REG_IRQ_LATCH:
        state->irq_latch = value;
        state->irq_latch_known = true;
        return AI2_MAPPER_WRITE_APPLIED;

    case AI2_MMC3_REG_IRQ_RELOAD:
        state->irq_counter = 0;
        state->irq_counter_known = true;
        state->irq_reload_pending = true;
        state->irq_reload_known = true;
        return AI2_MAPPER_WRITE_APPLIED;

    case AI2_MMC3_REG_IRQ_DISABLE:
        state->irq_enabled = false;
        state->irq_enabled_known = true;
        state->irq_pending = false;
        state->irq_pending_known = true;
        return AI2_MAPPER_WRITE_APPLIED;

    case AI2_MMC3_REG_IRQ_ENABLE:
        state->irq_enabled = true;
        state->irq_enabled_known = true;
        return AI2_MAPPER_WRITE_APPLIED;

    case AI2_MMC3_REG_NONE:
    default:
        return AI2_MAPPER_WRITE_NOT_MAPPER;
    }
}

Ai2IrqClockResult ai2_mmc3_clock_irq_counter(Ai2Mmc3State *state)
{
    if(!state->irq_latch_known || !state->irq_counter_known ||
       !state->irq_reload_known || !state->irq_enabled_known) {
        return AI2_IRQ_CLOCK_AMBIGUOUS_POWER_STATE;
    }

    if(state->irq_counter == 0u || state->irq_reload_pending) {
        state->irq_counter = state->irq_latch;
    } else {
        state->irq_counter--;
    }

    /* Exact USA board uses a Sharp MMC3B: zero after clock asserts if enabled. */
    if(state->irq_counter == 0u && state->irq_enabled) {
        state->irq_pending = true;
        state->irq_pending_known = true;
    } else if(!state->irq_pending_known) {
        state->irq_pending = false;
        state->irq_pending_known = true;
    }

    state->irq_reload_pending = false;
    state->irq_reload_known = true;
    return AI2_IRQ_CLOCKED;
}

void ai2_mmc3_clock_m2_falling_edge(Ai2Mmc3State *state)
{
    if(state->ppu_a12_known && !state->ppu_a12_high &&
       state->a12_low_m2_falls < AI2_MMC3_A12_LOW_M2_FALLS) {
        state->a12_low_m2_falls++;
    }
}

Ai2IrqClockResult ai2_mmc3_observe_ppu_address(Ai2Mmc3State *state, uint16_t ppu_address)
{
    const bool a12_high = (ppu_address & 0x1000u) != 0;

    if(!state->ppu_a12_known) {
        state->ppu_a12_high = a12_high;
        state->ppu_a12_known = true;
        state->a12_low_m2_falls = 0;
        return AI2_IRQ_NO_CLOCK;
    }

    if(!a12_high) {
        if(state->ppu_a12_high) {
            state->a12_low_m2_falls = 0;
        }
        state->ppu_a12_high = false;
        return AI2_IRQ_NO_CLOCK;
    }

    if(state->ppu_a12_high) {
        return AI2_IRQ_NO_CLOCK;
    }

    const bool qualified = state->a12_low_m2_falls >= AI2_MMC3_A12_LOW_M2_FALLS;
    state->ppu_a12_high = true;
    state->a12_low_m2_falls = 0;
    return qualified ? ai2_mmc3_clock_irq_counter(state) : AI2_IRQ_NO_CLOCK;
}
