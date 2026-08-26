#include "ai2_v07_ppu.h"

#include <string.h>

#define AI2_PPU_STATUS_OVERFLOW 0x20u
#define AI2_PPU_STATUS_SPRITE0  0x40u
#define AI2_PPU_STATUS_VBLANK   0x80u

static const uint32_t ai2_v07_rgb[64] = {
    0x666666u,0x002A88u,0x1412A7u,0x3B00A4u,0x5C007Eu,0x6E0040u,0x6C0600u,0x561D00u,
    0x333500u,0x0B4800u,0x005200u,0x004F08u,0x00404Du,0x000000u,0x000000u,0x000000u,
    0xADADADu,0x155FD9u,0x4240FFu,0x7527FEu,0xA01ACCu,0xB71E7Bu,0xB53120u,0x994E00u,
    0x6B6D00u,0x388700u,0x0C9300u,0x008F32u,0x007C8Du,0x000000u,0x000000u,0x000000u,
    0xFFFEFFu,0x64B0FFu,0x9290FFu,0xC676FFu,0xF36AFFu,0xFE6ECCu,0xFE8170u,0xEA9E22u,
    0xBCBE00u,0x88D800u,0x5CE430u,0x45E082u,0x48CDDEu,0x4F4F4Fu,0x000000u,0x000000u,
    0xFFFEFFu,0xC0DFFFu,0xD3D2FFu,0xE8C8FFu,0xFBC2FFu,0xFEC4EAu,0xFECCC5u,0xF7D8A5u,
    0xE4E594u,0xCFEE96u,0xBDF4ABu,0xB3F3CCu,0xB5EBF2u,0xB8B8B8u,0x000000u,0x000000u
};

static uint8_t ai2_v07_reverse8(uint8_t v)
{
    v = (uint8_t)(((v & 0x55u) << 1) | ((v >> 1) & 0x55u));
    v = (uint8_t)(((v & 0x33u) << 2) | ((v >> 2) & 0x33u));
    v = (uint8_t)((v << 4) | (v >> 4));
    return v;
}

static void ai2_v07_fail(Ai2V05Runtime *rt, Ai2V07Ppu *ppu, Ai2V07PpuError error, uint16_t address)
{
    if(ppu != NULL && ppu->error == AI2_V07_PPU_OK) {
        ppu->error = error;
    }
    if(rt != NULL && !ai2_v06_has_trap(rt)) {
        ai2_v06_raise_device_trap(rt, AI2_V06_TRAP_DEVICE_REJECTED, address, (uint8_t)error, 0);
    }
}

static uint8_t ai2_v07_palette_index(uint16_t address)
{
    uint8_t index = (uint8_t)(address & 0x1Fu);
    if((index & 0x13u) == 0x10u) {
        index &= 0x0Fu;
    }
    return index;
}

static int ai2_v07_observe_address(Ai2V05Runtime *rt, Ai2V07Ppu *ppu, uint16_t address)
{
    Ai2IrqClockResult r;
    if(rt == NULL || ppu == NULL) {
        return -1;
    }
    address &= 0x3FFFu;
    ppu->last_bus_address = address;
    r = ai2_mmc3_observe_ppu_address(&rt->mapper, address);
    if(r == AI2_IRQ_CLOCK_AMBIGUOUS_POWER_STATE) {
        /* A qualified A12 edge may occur before the game initializes the IRQ
         * latch/counter. If $E000 has already made IRQ output definitely
         * disabled, that internal counter uncertainty is not observable and is
         * deliberately preserved as unknown. Once IRQ output can be enabled,
         * an unresolved clock is fatal rather than guessed. */
        if(rt->mapper.irq_enabled_known && !rt->mapper.irq_enabled &&
           rt->mapper.irq_pending_known && !rt->mapper.irq_pending) {
            ppu->mapper_a12_ambiguous_while_irq_disabled++;
            return 0;
        }
        ai2_v07_fail(rt, ppu, AI2_V07_PPU_ERR_MAPPER_UNRESOLVED, address);
        return -1;
    }
    if(r == AI2_IRQ_CLOCKED) {
        ppu->mapper_a12_clocks++;
    }
    return 0;
}

int ai2_v07_ppu_memory_read(Ai2V05Runtime *rt, Ai2V07Ppu *ppu, uint16_t address, uint8_t *value)
{
    Ai2MapResult m;
    if(rt == NULL || ppu == NULL || value == NULL) {
        return -1;
    }
    address &= 0x3FFFu;
    if(ai2_v07_observe_address(rt, ppu, address) != 0) {
        return -1;
    }
    ppu->ppu_memory_reads++;
    if(address >= 0x3F00u) {
        *value = ppu->palette[ai2_v07_palette_index(address)];
        return 0;
    }
    m = ai2_mmc3_map_ppu_read(&rt->mapper, address);
    if(m.kind == AI2_MAP_CHR_ROM) {
        if(m.segment_offset >= rt->chr_rom_size) {
            ai2_v07_fail(rt, ppu, AI2_V07_PPU_ERR_BAD_MAPPING, address);
            return -1;
        }
        *value = rt->chr_rom[m.segment_offset];
        return 0;
    }
    if(m.kind == AI2_MAP_CIRAM) {
        if(m.segment_offset >= sizeof(ppu->ciram)) {
            ai2_v07_fail(rt, ppu, AI2_V07_PPU_ERR_BAD_MAPPING, address);
            return -1;
        }
        *value = ppu->ciram[m.segment_offset];
        return 0;
    }
    if(m.kind == AI2_MAP_UNRESOLVED_POWER_STATE) {
        ai2_v07_fail(rt, ppu, AI2_V07_PPU_ERR_MAPPER_UNRESOLVED, address);
        return -1;
    }
    ai2_v07_fail(rt, ppu, AI2_V07_PPU_ERR_BAD_MAPPING, address);
    return -1;
}

int ai2_v07_ppu_memory_write(Ai2V05Runtime *rt, Ai2V07Ppu *ppu, uint16_t address, uint8_t value)
{
    Ai2MapResult m;
    if(rt == NULL || ppu == NULL) {
        return -1;
    }
    address &= 0x3FFFu;
    if(ai2_v07_observe_address(rt, ppu, address) != 0) {
        return -1;
    }
    ppu->ppu_memory_writes++;
    if(address >= 0x3F00u) {
        ppu->palette[ai2_v07_palette_index(address)] = (uint8_t)(value & 0x3Fu);
        return 0;
    }
    m = ai2_mmc3_map_ppu_read(&rt->mapper, address);
    if(m.kind == AI2_MAP_CIRAM) {
        if(m.segment_offset >= sizeof(ppu->ciram)) {
            ai2_v07_fail(rt, ppu, AI2_V07_PPU_ERR_BAD_MAPPING, address);
            return -1;
        }
        ppu->ciram[m.segment_offset] = value;
        return 0;
    }
    if(m.kind == AI2_MAP_CHR_ROM) {
        /* TLROM-03 has CHR ROM. PPU writes drive the bus but cannot alter it. */
        return 0;
    }
    if(m.kind == AI2_MAP_UNRESOLVED_POWER_STATE) {
        ai2_v07_fail(rt, ppu, AI2_V07_PPU_ERR_MAPPER_UNRESOLVED, address);
        return -1;
    }
    ai2_v07_fail(rt, ppu, AI2_V07_PPU_ERR_BAD_MAPPING, address);
    return -1;
}

static int ai2_v07_rendering(const Ai2V07Ppu *ppu)
{
    return ppu != NULL && (ppu->mask & 0x18u) != 0u;
}

int ai2_v07_ppu_rendering_enabled(const Ai2V07Ppu *ppu)
{
    return ai2_v07_rendering(ppu);
}

static void ai2_v07_set_nmi_line(Ai2V05Runtime *rt, Ai2V07Ppu *ppu, int asserted)
{
    uint8_t next = asserted ? 1u : 0u;
    if(ppu->nmi_line == next) {
        return;
    }
    ppu->nmi_line = next;
    if(next) {
        ai2_v06_request_nmi(rt);
    } else {
        ai2_v06_clear_nmi(rt);
    }
}

static void ai2_v07_update_nmi(Ai2V05Runtime *rt, Ai2V07Ppu *ppu)
{
    ai2_v07_set_nmi_line(rt, ppu,
        ((ppu->status & AI2_PPU_STATUS_VBLANK) != 0u) && ((ppu->ctrl & 0x80u) != 0u));
}

void ai2_v07_ppu_init(Ai2V07Ppu *ppu, uint8_t ciram_fill, uint8_t oam_fill)
{
    if(ppu == NULL) {
        return;
    }
    memset(ppu, 0, sizeof(*ppu));
    memset(ppu->ciram, ciram_fill, sizeof(ppu->ciram));
    memset(ppu->oam, oam_fill, sizeof(ppu->oam));
    memset(ppu->secondary_oam, 0xFF, sizeof(ppu->secondary_oam));
    /* V10 reset-phase correction: deterministic NTSC power-on starts at
     * scanline 0, dot 4. ai2_v06_reset then charges exactly seven CPU cycles
     * (21 PPU dots), reaching the RESET entry at scanline 0, dot 25. */
    ppu->scanline = 0;
    ppu->dot = 4;
    ppu->error = AI2_V07_PPU_OK;
}

void ai2_v07_ppu_bind(Ai2V05Runtime *rt, Ai2V07Ppu *ppu)
{
    Ai2V06DeviceHooks hooks;
    if(rt == NULL || ppu == NULL) {
        return;
    }
    hooks = rt->devices;
    hooks.user = ppu;
    hooks.ppu_read = ai2_v07_ppu_cpu_read;
    hooks.ppu_write = ai2_v07_ppu_cpu_write;
    hooks.ppu_tick = ai2_v07_ppu_tick;
    ai2_v06_set_devices(rt, &hooks);
}

static void ai2_v07_inc_x(Ai2V07Ppu *ppu)
{
    if((ppu->v & 0x001Fu) == 31u) {
        ppu->v &= (uint16_t)~0x001Fu;
        ppu->v ^= 0x0400u;
    } else {
        ppu->v++;
    }
}

static void ai2_v07_inc_y(Ai2V07Ppu *ppu)
{
    if((ppu->v & 0x7000u) != 0x7000u) {
        ppu->v += 0x1000u;
    } else {
        uint16_t y;
        ppu->v &= (uint16_t)~0x7000u;
        y = (ppu->v & 0x03E0u) >> 5;
        if(y == 29u) {
            y = 0u;
            ppu->v ^= 0x0800u;
        } else if(y == 31u) {
            y = 0u;
        } else {
            y++;
        }
        ppu->v = (uint16_t)((ppu->v & (uint16_t)~0x03E0u) | (y << 5));
    }
}

static void ai2_v07_increment_2007(Ai2V07Ppu *ppu)
{
    if(ai2_v07_rendering(ppu) && (ppu->scanline < 240 || ppu->scanline == 261)) {
        ai2_v07_inc_x(ppu);
        ai2_v07_inc_y(ppu);
    } else {
        ppu->v = (uint16_t)((ppu->v + ((ppu->ctrl & 0x04u) ? 32u : 1u)) & 0x7FFFu);
    }
}

int ai2_v07_ppu_cpu_read(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t *value)
{
    Ai2V07Ppu *ppu = (Ai2V07Ppu *)user;
    uint16_t reg;
    uint8_t v;
    if(rt == NULL || ppu == NULL || value == NULL) {
        return -1;
    }
    reg = (uint16_t)(0x2000u | (address & 7u));
    ppu->ppu_open_bus = *value;
    switch(reg) {
        case 0x2002u:
            v = (uint8_t)((ppu->status & 0xE0u) | (*value & 0x1Fu));
            if(ppu->scanline == 241 && ppu->dot <= 1u) {
                v &= (uint8_t)~AI2_PPU_STATUS_VBLANK;
                ppu->suppress_vblank = 1u;
            }
            ppu->status &= (uint8_t)~AI2_PPU_STATUS_VBLANK;
            ppu->write_toggle = 0u;
            ai2_v07_update_nmi(rt, ppu);
            *value = v;
            break;
        case 0x2004u:
            if(ai2_v07_rendering(ppu) && (ppu->scanline < 240 || ppu->scanline == 261)) {
                *value = ppu->secondary_oam[ppu->oam_addr & 0x1Fu];
            } else {
                *value = ppu->oam[ppu->oam_addr];
            }
            break;
        case 0x2007u: {
            uint16_t a = (uint16_t)(ppu->v & 0x3FFFu);
            if(a >= 0x3F00u) {
                uint8_t pal = ppu->palette[ai2_v07_palette_index(a)];
                uint8_t under = 0u;
                *value = (uint8_t)((pal & 0x3Fu) | (*value & 0xC0u));
                if(ai2_v07_ppu_memory_read(rt, ppu, (uint16_t)(a - 0x1000u), &under) != 0) return -1;
                ppu->read_buffer = under;
            } else {
                uint8_t fetched = 0u;
                *value = ppu->read_buffer;
                if(ai2_v07_ppu_memory_read(rt, ppu, a, &fetched) != 0) return -1;
                ppu->read_buffer = fetched;
            }
            ai2_v07_increment_2007(ppu);
            break;
        }
        default:
            /* Write-only PPU registers read as CPU/PPU I/O open bus. */
            break;
    }
    ppu->ppu_open_bus = *value;
    return ppu->error == AI2_V07_PPU_OK ? 0 : -1;
}

int ai2_v07_ppu_cpu_write(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t value)
{
    Ai2V07Ppu *ppu = (Ai2V07Ppu *)user;
    uint16_t reg;
    if(rt == NULL || ppu == NULL) {
        return -1;
    }
    reg = (uint16_t)(0x2000u | (address & 7u));
    ppu->ppu_open_bus = value;
    switch(reg) {
        case 0x2000u:
            ppu->ctrl = value;
            ppu->t = (uint16_t)((ppu->t & (uint16_t)~0x0C00u) | ((uint16_t)(value & 3u) << 10));
            ai2_v07_update_nmi(rt, ppu);
            break;
        case 0x2001u:
            ppu->mask = value;
            break;
        case 0x2003u:
            ppu->oam_addr = value;
            break;
        case 0x2004u:
            if(ai2_v07_rendering(ppu) && (ppu->scanline < 240 || ppu->scanline == 261)) {
                ppu->oam_addr = (uint8_t)((ppu->oam_addr + 4u) & 0xFCu);
            } else {
                if((ppu->oam_addr & 3u) == 2u) {
                    value &= 0xE3u;
                }
                ppu->oam[ppu->oam_addr++] = value;
            }
            break;
        case 0x2005u:
            if(ppu->write_toggle == 0u) {
                ppu->fine_x = (uint8_t)(value & 7u);
                ppu->t = (uint16_t)((ppu->t & (uint16_t)~0x001Fu) | ((uint16_t)value >> 3));
                ppu->write_toggle = 1u;
            } else {
                ppu->t = (uint16_t)((ppu->t & (uint16_t)~0x73E0u) |
                    ((uint16_t)(value & 0xF8u) << 2) | ((uint16_t)(value & 7u) << 12));
                ppu->write_toggle = 0u;
            }
            break;
        case 0x2006u:
            if(ppu->write_toggle == 0u) {
                ppu->t = (uint16_t)((ppu->t & 0x00FFu) | ((uint16_t)(value & 0x3Fu) << 8));
                ppu->write_toggle = 1u;
            } else {
                ppu->t = (uint16_t)((ppu->t & 0x7F00u) | value);
                ppu->v = ppu->t;
                ppu->write_toggle = 0u;
                if(!ai2_v07_rendering(ppu) || ppu->scanline >= 240) {
                    if(ai2_v07_observe_address(rt, ppu, (uint16_t)(ppu->v & 0x3FFFu)) != 0) return -1;
                }
            }
            break;
        case 0x2007u:
            if(ai2_v07_ppu_memory_write(rt, ppu, (uint16_t)(ppu->v & 0x3FFFu), value) != 0) return -1;
            ai2_v07_increment_2007(ppu);
            break;
        default:
            break;
    }
    return ppu->error == AI2_V07_PPU_OK ? 0 : -1;
}

static void ai2_v07_bg_load(Ai2V07Ppu *ppu)
{
    ppu->bg_shift_lo = (uint16_t)((ppu->bg_shift_lo & 0xFF00u) | ppu->next_bg_lo);
    ppu->bg_shift_hi = (uint16_t)((ppu->bg_shift_hi & 0xFF00u) | ppu->next_bg_hi);
    ppu->at_shift_lo = (uint16_t)((ppu->at_shift_lo & 0xFF00u) | ((ppu->next_attr & 1u) ? 0x00FFu : 0u));
    ppu->at_shift_hi = (uint16_t)((ppu->at_shift_hi & 0xFF00u) | ((ppu->next_attr & 2u) ? 0x00FFu : 0u));
}

static void ai2_v07_bg_shift(Ai2V07Ppu *ppu)
{
    ppu->bg_shift_lo <<= 1;
    ppu->bg_shift_hi <<= 1;
    ppu->at_shift_lo <<= 1;
    ppu->at_shift_hi <<= 1;
}

static int ai2_v07_fetch_bg(Ai2V05Runtime *rt, Ai2V07Ppu *ppu)
{
    uint16_t d = ppu->dot;
    uint8_t phase = (uint8_t)((d - 1u) & 7u);
    uint8_t value = 0u;
    if(phase == 0u) {
        uint16_t a;
        ai2_v07_bg_load(ppu);
        a = (uint16_t)(0x2000u | (ppu->v & 0x0FFFu));
        if(ai2_v07_ppu_memory_read(rt, ppu, a, &ppu->next_tile) != 0) return -1;
    } else if(phase == 2u) {
        uint16_t a = (uint16_t)(0x23C0u | (ppu->v & 0x0C00u) |
            ((ppu->v >> 4) & 0x38u) | ((ppu->v >> 2) & 7u));
        uint8_t shift = (uint8_t)(((ppu->v >> 4) & 4u) | (ppu->v & 2u));
        if(ai2_v07_ppu_memory_read(rt, ppu, a, &value) != 0) return -1;
        ppu->next_attr = (uint8_t)((value >> shift) & 3u);
    } else if(phase == 4u) {
        uint16_t a = (uint16_t)(((ppu->ctrl & 0x10u) ? 0x1000u : 0u) +
            (uint16_t)ppu->next_tile * 16u + ((ppu->v >> 12) & 7u));
        if(ai2_v07_ppu_memory_read(rt, ppu, a, &ppu->next_bg_lo) != 0) return -1;
    } else if(phase == 6u) {
        uint16_t a = (uint16_t)(((ppu->ctrl & 0x10u) ? 0x1000u : 0u) +
            (uint16_t)ppu->next_tile * 16u + ((ppu->v >> 12) & 7u) + 8u);
        if(ai2_v07_ppu_memory_read(rt, ppu, a, &ppu->next_bg_hi) != 0) return -1;
    } else if(phase == 7u) {
        ai2_v07_inc_x(ppu);
    }
    return 0;
}

static void ai2_v07_evaluate_sprites(Ai2V07Ppu *ppu)
{
    unsigned i;
    uint8_t target = (ppu->scanline == 261) ? 0u : (uint8_t)(ppu->scanline + 1);
    uint8_t height = (ppu->ctrl & 0x20u) ? 16u : 8u;
    ppu->next_sprite_count = 0u;
    ppu->sprite_zero_next = 0u;
    memset(ppu->secondary_oam, 0xFF, sizeof(ppu->secondary_oam));
    memset(ppu->next_sprites, 0, sizeof(ppu->next_sprites));
    for(i = 0; i < 64u; i++) {
        uint8_t y = ppu->oam[i * 4u];
        uint8_t row = (uint8_t)(target - (uint8_t)(y + 1u));
        if(row < height) {
            if(ppu->next_sprite_count < AI2_V07_SPRITES_PER_LINE) {
                Ai2V07Sprite *s = &ppu->next_sprites[ppu->next_sprite_count];
                unsigned base = i * 4u;
                s->y = ppu->oam[base];
                s->tile = ppu->oam[base + 1u];
                s->attr = ppu->oam[base + 2u];
                s->x = ppu->oam[base + 3u];
                s->oam_index = (uint8_t)i;
                s->valid = 1u;
                s->row = row;
                memcpy(&ppu->secondary_oam[ppu->next_sprite_count * 4u], &ppu->oam[base], 4u);
                if(i == 0u) ppu->sprite_zero_next = 1u;
                ppu->next_sprite_count++;
            } else {
                /* The target requires overflow state. We intentionally model the
                 * documented ninth-in-range condition, not the transistor-level
                 * diagonal OAM overflow bug; that limit is recorded in V07 docs. */
                ppu->status |= AI2_PPU_STATUS_OVERFLOW;
                break;
            }
        }
    }
}

static uint16_t ai2_v07_sprite_pattern_addr(const Ai2V07Ppu *ppu, const Ai2V07Sprite *s, int high)
{
    uint8_t tile = s->valid ? s->tile : 0xFFu;
    uint8_t row = s->valid ? s->row : 0u;
    if(s->valid && (s->attr & 0x80u)) {
        row = (uint8_t)(((ppu->ctrl & 0x20u) ? 15u : 7u) - row);
    }
    if(ppu->ctrl & 0x20u) {
        uint16_t base = (tile & 1u) ? 0x1000u : 0u;
        tile &= 0xFEu;
        if(row >= 8u) {
            tile++;
            row -= 8u;
        }
        return (uint16_t)(base + (uint16_t)tile * 16u + row + (high ? 8u : 0u));
    }
    return (uint16_t)(((ppu->ctrl & 0x08u) ? 0x1000u : 0u) +
        (uint16_t)tile * 16u + row + (high ? 8u : 0u));
}

static int ai2_v07_sprite_fetch(Ai2V05Runtime *rt, Ai2V07Ppu *ppu)
{
    uint16_t rel = (uint16_t)(ppu->dot - 257u);
    unsigned slot = rel >> 3;
    uint8_t phase = (uint8_t)(rel & 7u);
    uint8_t value = 0u;
    Ai2V07Sprite dummy;
    Ai2V07Sprite *s;
    if(slot >= AI2_V07_SPRITES_PER_LINE) return 0;
    memset(&dummy, 0, sizeof(dummy));
    dummy.tile = 0xFFu;
    dummy.attr = 0xFFu;
    s = slot < ppu->next_sprite_count ? &ppu->next_sprites[slot] : &dummy;
    if(phase == 0u || phase == 2u) {
        return ai2_v07_ppu_memory_read(rt, ppu, (uint16_t)(0x2000u | (ppu->v & 0x0FFFu)), &value);
    }
    if(phase == 4u) {
        uint16_t a = ai2_v07_sprite_pattern_addr(ppu, s, 0);
        if(ai2_v07_ppu_memory_read(rt, ppu, a, &value) != 0) return -1;
        if(s->valid) s->pattern_lo = (s->attr & 0x40u) ? ai2_v07_reverse8(value) : value;
    } else if(phase == 6u) {
        uint16_t a = ai2_v07_sprite_pattern_addr(ppu, s, 1);
        if(ai2_v07_ppu_memory_read(rt, ppu, a, &value) != 0) return -1;
        if(s->valid) s->pattern_hi = (s->attr & 0x40u) ? ai2_v07_reverse8(value) : value;
    }
    return 0;
}

static uint16_t ai2_v07_output_code(const Ai2V07Ppu *ppu, uint8_t palette_value)
{
    uint8_t color = (uint8_t)(palette_value & ((ppu->mask & 1u) ? 0x30u : 0x3Fu));
    uint8_t emphasis = (uint8_t)((ppu->mask >> 5) & 7u);
    return (uint16_t)(color | ((uint16_t)emphasis << 6));
}

static void ai2_v07_render_pixel(Ai2V07Ppu *ppu)
{
    unsigned x = (unsigned)ppu->dot - 1u;
    unsigned y = (unsigned)ppu->scanline;
    uint8_t bg_pixel = 0u, bg_pal = 0u;
    uint8_t sp_pixel = 0u, sp_pal = 0u, sp_priority = 0u, sp_zero = 0u;
    uint8_t palette_addr;
    unsigned i;
    uint16_t bit;

    if((ppu->mask & 0x08u) != 0u && (x >= 8u || (ppu->mask & 0x02u) != 0u)) {
        bit = (uint16_t)(0x8000u >> ppu->fine_x);
        bg_pixel = (uint8_t)(((ppu->bg_shift_lo & bit) ? 1u : 0u) |
            ((ppu->bg_shift_hi & bit) ? 2u : 0u));
        bg_pal = (uint8_t)(((ppu->at_shift_lo & bit) ? 1u : 0u) |
            ((ppu->at_shift_hi & bit) ? 2u : 0u));
    }

    if((ppu->mask & 0x10u) != 0u && (x >= 8u || (ppu->mask & 0x04u) != 0u)) {
        for(i = 0; i < ppu->sprite_count; i++) {
            Ai2V07Sprite *s = &ppu->sprites[i];
            uint8_t pix;
            if(!s->valid || s->x != 0u) continue;
            pix = (uint8_t)(((s->pattern_lo & 0x80u) ? 1u : 0u) |
                ((s->pattern_hi & 0x80u) ? 2u : 0u));
            if(pix != 0u) {
                sp_pixel = pix;
                sp_pal = (uint8_t)(s->attr & 3u);
                sp_priority = (uint8_t)((s->attr >> 5) & 1u);
                sp_zero = (uint8_t)(s->oam_index == 0u);
                break;
            }
        }
    }

    if(bg_pixel != 0u && sp_pixel != 0u && sp_zero && x != 255u && y != 239u &&
       (ppu->mask & 0x18u) == 0x18u) {
        ppu->status |= AI2_PPU_STATUS_SPRITE0;
    }

    if(bg_pixel == 0u && sp_pixel == 0u) {
        palette_addr = 0u;
    } else if(bg_pixel == 0u) {
        palette_addr = (uint8_t)(0x10u | (sp_pal << 2) | sp_pixel);
    } else if(sp_pixel == 0u) {
        palette_addr = (uint8_t)((bg_pal << 2) | bg_pixel);
    } else if(sp_priority == 0u) {
        palette_addr = (uint8_t)(0x10u | (sp_pal << 2) | sp_pixel);
    } else {
        palette_addr = (uint8_t)((bg_pal << 2) | bg_pixel);
    }
    ppu->frame_indexed[y * AI2_V07_SCREEN_WIDTH + x] =
        ai2_v07_output_code(ppu, ppu->palette[ai2_v07_palette_index((uint16_t)(0x3F00u + palette_addr))]);
}

static void ai2_v07_shift_sprites(Ai2V07Ppu *ppu)
{
    unsigned i;
    for(i = 0; i < ppu->sprite_count; i++) {
        Ai2V07Sprite *s = &ppu->sprites[i];
        if(s->x != 0u) {
            s->x--;
        } else {
            s->pattern_lo <<= 1;
            s->pattern_hi <<= 1;
        }
    }
}

static void ai2_v07_start_scanline(Ai2V07Ppu *ppu)
{
    if(ppu->scanline >= 0 && ppu->scanline < 240) {
        ppu->sprite_count = ppu->next_sprite_count;
        ppu->sprite_zero_current = ppu->sprite_zero_next;
        memcpy(ppu->sprites, ppu->next_sprites, sizeof(ppu->sprites));
    } else {
        ppu->sprite_count = 0u;
        ppu->sprite_zero_current = 0u;
        memset(ppu->sprites, 0, sizeof(ppu->sprites));
    }
}

void ai2_v07_ppu_tick(Ai2V05Runtime *rt, void *user)
{
    Ai2V07Ppu *ppu = (Ai2V07Ppu *)user;
    int rendering;
    if(rt == NULL || ppu == NULL || ppu->error != AI2_V07_PPU_OK || ai2_v06_has_trap(rt)) {
        return;
    }
    rendering = ai2_v07_rendering(ppu);

    if(ppu->dot == 0u) {
        ai2_v07_start_scanline(ppu);
    }

    if(ppu->scanline == 241 && ppu->dot == 1u) {
        if(!ppu->suppress_vblank) {
            ppu->status |= AI2_PPU_STATUS_VBLANK;
            ai2_v07_update_nmi(rt, ppu);
        }
        ppu->suppress_vblank = 0u;
    }
    if(ppu->scanline == 261 && ppu->dot == 1u) {
        ppu->status &= (uint8_t)~(AI2_PPU_STATUS_VBLANK | AI2_PPU_STATUS_SPRITE0 | AI2_PPU_STATUS_OVERFLOW);
        ppu->suppress_vblank = 0u;
        ai2_v07_update_nmi(rt, ppu);
    }

    if(ppu->scanline >= 0 && ppu->scanline < 240 && ppu->dot >= 1u && ppu->dot <= 256u) {
        ai2_v07_render_pixel(ppu);
    }

    if(rendering && ((ppu->scanline >= 0 && ppu->scanline < 240) || ppu->scanline == 261)) {
        if((ppu->dot >= 1u && ppu->dot <= 256u) || (ppu->dot >= 321u && ppu->dot <= 336u)) {
            if(ai2_v07_fetch_bg(rt, ppu) != 0) return;
        }
        if(ppu->dot == 256u) ai2_v07_inc_y(ppu);
        if(ppu->dot == 257u) {
            ppu->v = (uint16_t)((ppu->v & (uint16_t)~0x041Fu) | (ppu->t & 0x041Fu));
            ppu->oam_addr = 0u;
            ai2_v07_evaluate_sprites(ppu);
        }
        if(ppu->dot >= 257u && ppu->dot <= 320u) {
            ppu->oam_addr = 0u;
            if(ai2_v07_sprite_fetch(rt, ppu) != 0) return;
        }
        if(ppu->scanline == 261 && ppu->dot >= 280u && ppu->dot <= 304u) {
            ppu->v = (uint16_t)((ppu->v & (uint16_t)~0x7BE0u) | (ppu->t & 0x7BE0u));
        }
        if(ppu->dot == 337u || ppu->dot == 339u) {
            uint8_t ignored = 0u;
            if(ai2_v07_ppu_memory_read(rt, ppu, (uint16_t)(0x2000u | (ppu->v & 0x0FFFu)), &ignored) != 0) return;
        }
        if((ppu->dot >= 1u && ppu->dot <= 256u) || (ppu->dot >= 321u && ppu->dot <= 336u)) {
            ai2_v07_bg_shift(ppu);
        }
    }

    if(ppu->scanline >= 0 && ppu->scanline < 240 && ppu->dot >= 1u && ppu->dot <= 256u) {
        ai2_v07_shift_sprites(ppu);
    }

    if(ppu->scanline == 261 && ppu->dot == 339u && rendering && (ppu->frame_count & 1u)) {
        ppu->dot = 0u;
        ppu->scanline = 0;
        return;
    }

    if(ppu->dot == 340u) {
        ppu->dot = 0u;
        if(ppu->scanline == 261) {
            ppu->scanline = 0;
        } else {
            ppu->scanline++;
            if(ppu->scanline == 240) {
                ppu->frame_generation++;
                ppu->frame_count++;
            }
        }
    } else {
        ppu->dot++;
    }
}

static uint8_t ai2_v07_scale(uint8_t v, unsigned num, unsigned den)
{
    return (uint8_t)(((unsigned)v * num) / den);
}

static uint32_t ai2_v07_code_to_bgra(uint16_t code)
{
    uint32_t rgb = ai2_v07_rgb[code & 0x3Fu];
    uint8_t r = (uint8_t)(rgb >> 16);
    uint8_t g = (uint8_t)(rgb >> 8);
    uint8_t b = (uint8_t)rgb;
    uint8_t emph = (uint8_t)((code >> 6) & 7u);
    /* Deterministic host preview conversion. PPU logic preserves the raw
     * 6-bit color plus three emphasis bits in the indexed API; exact analog
     * NTSC decoder colorimetry is intentionally not a construction proof. */
    if(emph & 1u) { g = ai2_v07_scale(g, 3u, 4u); b = ai2_v07_scale(b, 3u, 4u); }
    if(emph & 2u) { r = ai2_v07_scale(r, 3u, 4u); b = ai2_v07_scale(b, 3u, 4u); }
    if(emph & 4u) { r = ai2_v07_scale(r, 3u, 4u); g = ai2_v07_scale(g, 3u, 4u); }
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

int ai2_v07_ppu_copy_indexed(const Ai2V07Ppu *ppu, uint16_t *dst, size_t pixels, uint64_t *generation)
{
    if(ppu == NULL || dst == NULL || pixels < AI2_V07_SCREEN_PIXELS) return -1;
    memcpy(dst, ppu->frame_indexed, sizeof(ppu->frame_indexed));
    if(generation != NULL) *generation = ppu->frame_generation;
    return 0;
}

int ai2_v07_ppu_copy_bgra(const Ai2V07Ppu *ppu, uint32_t *dst, size_t pixels, uint64_t *generation)
{
    size_t i;
    if(ppu == NULL || dst == NULL || pixels < AI2_V07_SCREEN_PIXELS) return -1;
    for(i = 0; i < AI2_V07_SCREEN_PIXELS; i++) dst[i] = ai2_v07_code_to_bgra(ppu->frame_indexed[i]);
    if(generation != NULL) *generation = ppu->frame_generation;
    return 0;
}

Ai2V07PpuError ai2_v07_ppu_error(const Ai2V07Ppu *ppu)
{
    return ppu == NULL ? AI2_V07_PPU_ERR_NULL : ppu->error;
}
