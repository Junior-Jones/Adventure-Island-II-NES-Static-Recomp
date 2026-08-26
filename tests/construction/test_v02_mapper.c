#include "ai2_mmc3_mapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks = 0;

#define CHECK(expr) do { \
    checks++; \
    if(!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while(0)

static int write_reg(Ai2Mmc3State *s, unsigned reg, unsigned mode_bits, uint8_t value)
{
    CHECK(ai2_mmc3_cpu_write(s, 0x8000u, (uint8_t)(mode_bits | (reg & 7u))) == AI2_MAPPER_WRITE_APPLIED);
    CHECK(ai2_mmc3_cpu_write(s, 0x8001u, value) == AI2_MAPPER_WRITE_APPLIED);
    return 0;
}

static int test_power_and_reset(void)
{
    Ai2Mmc3State s, before;
    ai2_mmc3_power_on_unknown(&s);

    CHECK(ai2_mmc3_map_cpu_read(&s, 0x401Fu).kind == AI2_MAP_NOT_CARTRIDGE);
    CHECK(ai2_mmc3_map_cpu_read(&s, 0x4020u).kind == AI2_MAP_OPEN_BUS);
    CHECK(ai2_mmc3_map_cpu_read(&s, 0x6000u).kind == AI2_MAP_OPEN_BUS);
    CHECK(ai2_mmc3_map_cpu_read(&s, 0x7FFFu).kind == AI2_MAP_OPEN_BUS);
    CHECK(ai2_mmc3_map_cpu_read(&s, 0x8000u).kind == AI2_MAP_UNRESOLVED_POWER_STATE);
    CHECK(ai2_mmc3_map_cpu_read(&s, 0xA000u).kind == AI2_MAP_UNRESOLVED_POWER_STATE);
    CHECK(ai2_mmc3_map_cpu_read(&s, 0xC000u).kind == AI2_MAP_UNRESOLVED_POWER_STATE);

    Ai2MapResult fixed = ai2_mmc3_map_cpu_read(&s, 0xE000u);
    CHECK(fixed.kind == AI2_MAP_PRG_ROM);
    CHECK(fixed.physical_bank == 15u);
    CHECK(fixed.segment_offset == 15u * 0x2000u);

    CHECK(ai2_mmc3_map_ppu_read(&s, 0x0000u).kind == AI2_MAP_UNRESOLVED_POWER_STATE);
    CHECK(ai2_mmc3_map_ppu_read(&s, 0x2000u).kind == AI2_MAP_UNRESOLVED_POWER_STATE);
    CHECK(ai2_mmc3_map_ppu_read(&s, 0x3F00u).kind == AI2_MAP_NOT_CARTRIDGE);

    before = s;
    ai2_mmc3_cpu_reset(&s);
    CHECK(memcmp(&s, &before, sizeof(s)) == 0);

    CHECK(ai2_mmc3_cpu_write(&s, 0x8001u, 0x12u) == AI2_MAPPER_WRITE_AMBIGUOUS_POWER_STATE);
    CHECK(ai2_mmc3_cpu_write(&s, 0x7FFFu, 0x12u) == AI2_MAPPER_WRITE_NOT_MAPPER);
    return 0;
}

static int test_prg_mapping(void)
{
    for(unsigned mode = 0; mode < 2; mode++) {
        const unsigned mode_bits = mode ? 0x40u : 0u;
        for(unsigned value = 0; value < 256; value++) {
            Ai2Mmc3State s;
            ai2_mmc3_power_on_unknown(&s);
            if(write_reg(&s, 6, mode_bits, (uint8_t)value)) return 1;
            if(write_reg(&s, 7, mode_bits, (uint8_t)(255u - value))) return 1;

            const uint8_t r6 = (uint8_t)(value & 0x0Fu);
            const uint8_t r7 = (uint8_t)((255u - value) & 0x0Fu);
            Ai2MapResult a = ai2_mmc3_map_cpu_read(&s, 0x8000u);
            Ai2MapResult b = ai2_mmc3_map_cpu_read(&s, 0xA123u);
            Ai2MapResult c = ai2_mmc3_map_cpu_read(&s, 0xDFFFu);
            Ai2MapResult d = ai2_mmc3_map_cpu_read(&s, 0xFFFFu);

            CHECK(a.kind == AI2_MAP_PRG_ROM);
            CHECK(b.kind == AI2_MAP_PRG_ROM);
            CHECK(c.kind == AI2_MAP_PRG_ROM);
            CHECK(d.kind == AI2_MAP_PRG_ROM);
            CHECK(a.physical_bank == (mode ? 14u : r6));
            CHECK(b.physical_bank == r7);
            CHECK(c.physical_bank == (mode ? r6 : 14u));
            CHECK(d.physical_bank == 15u);
            CHECK(b.bank_offset == 0x0123u);
            CHECK(c.bank_offset == 0x1FFFu);
            CHECK(d.segment_offset == (15u * 0x2000u + 0x1FFFu));
        }
    }

    for(unsigned v = 0; v < 256; v++) {
        CHECK(ai2_mmc3_physical_prg_bank((uint8_t)v) == (uint8_t)(v & 0x0Fu));
    }
    return 0;
}

static unsigned expected_chr_reg(unsigned mode, unsigned slot, unsigned *pair)
{
    static const unsigned m0_reg[8] = {0,0,1,1,2,3,4,5};
    static const unsigned m0_pair[8] = {0,1,0,1,0,0,0,0};
    static const unsigned m1_reg[8] = {2,3,4,5,0,0,1,1};
    static const unsigned m1_pair[8] = {0,0,0,0,0,1,0,1};
    *pair = mode ? m1_pair[slot] : m0_pair[slot];
    return mode ? m1_reg[slot] : m0_reg[slot];
}

static int test_chr_mapping(void)
{
    for(unsigned mode = 0; mode < 2; mode++) {
        for(unsigned reg_under_test = 0; reg_under_test < 6; reg_under_test++) {
            for(unsigned value = 0; value < 256; value++) {
                Ai2Mmc3State s;
                ai2_mmc3_power_on_unknown(&s);
                const unsigned mode_bits = mode ? 0x80u : 0u;
                for(unsigned r = 0; r < 6; r++) {
                    if(write_reg(&s, r, mode_bits, r == reg_under_test ? (uint8_t)value : (uint8_t)(r * 7u))) return 1;
                }

                for(unsigned slot = 0; slot < 8; slot++) {
                    unsigned pair = 0;
                    unsigned reg = expected_chr_reg(mode, slot, &pair);
                    uint8_t raw = reg == reg_under_test ? (uint8_t)value : (uint8_t)(reg * 7u);
                    if(reg <= 1u) raw &= 0xFEu;
                    uint8_t expected = (uint8_t)((raw + pair) & 0x7Fu);
                    uint16_t addr = (uint16_t)(slot * 0x400u + 0x155u);
                    Ai2MapResult m = ai2_mmc3_map_ppu_read(&s, addr);
                    CHECK(m.kind == AI2_MAP_CHR_ROM);
                    CHECK(m.physical_bank == expected);
                    CHECK(m.bank_offset == 0x155u);
                    CHECK(m.segment_offset == (uint32_t)expected * 0x400u + 0x155u);
                }
            }
        }
    }

    for(unsigned v = 0; v < 256; v++) {
        CHECK(ai2_mmc3_physical_chr_bank((uint8_t)v) == (uint8_t)(v & 0x7Fu));
    }
    return 0;
}

static int test_nametable_mapping(void)
{
    for(unsigned mirror = 0; mirror < 2; mirror++) {
        Ai2Mmc3State s;
        ai2_mmc3_power_on_unknown(&s);
        CHECK(ai2_mmc3_cpu_write(&s, 0xA000u, (uint8_t)mirror) == AI2_MAPPER_WRITE_APPLIED);

        for(unsigned addr = 0x2000u; addr < 0x3F00u; addr++) {
            uint16_t normalized = (uint16_t)addr;
            if(normalized >= 0x3000u) normalized = (uint16_t)(normalized - 0x1000u);
            unsigned nt_offset = normalized - 0x2000u;
            unsigned table = (nt_offset >> 10) & 3u;
            unsigned expected_page = mirror ? ((table >> 1) & 1u) : (table & 1u);
            Ai2MapResult m = ai2_mmc3_map_ppu_read(&s, (uint16_t)addr);
            CHECK(m.kind == AI2_MAP_CIRAM);
            CHECK(m.ciram_page == expected_page);
            CHECK(m.bank_offset == (nt_offset & 0x3FFu));
            CHECK(m.segment_offset == expected_page * 0x400u + (nt_offset & 0x3FFu));
        }
    }
    return 0;
}

static Ai2Mmc3Register expected_decode(uint16_t address)
{
    if(address < 0x8000u) return AI2_MMC3_REG_NONE;
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

static int test_all_address_decoding(void)
{
    for(unsigned addr = 0; addr <= 0xFFFFu; addr++) {
        CHECK(ai2_mmc3_decode_cpu_write((uint16_t)addr) == expected_decode((uint16_t)addr));
    }

    for(unsigned mode = 0; mode < 2; mode++) {
        Ai2Mmc3State s;
        ai2_mmc3_power_on_unknown(&s);
        unsigned bits = mode ? 0x40u : 0u;
        if(write_reg(&s, 6, bits, 3u)) return 1;
        if(write_reg(&s, 7, bits, 5u)) return 1;
        for(unsigned addr = 0x8000u; addr <= 0xFFFFu; addr++) {
            Ai2MapResult m = ai2_mmc3_map_cpu_read(&s, (uint16_t)addr);
            unsigned expected_bank;
            if(addr < 0xA000u) expected_bank = mode ? 14u : 3u;
            else if(addr < 0xC000u) expected_bank = 5u;
            else if(addr < 0xE000u) expected_bank = mode ? 3u : 14u;
            else expected_bank = 15u;
            CHECK(m.kind == AI2_MAP_PRG_ROM);
            CHECK(m.physical_bank == expected_bank);
            CHECK(m.bank_offset == (addr & 0x1FFFu));
            CHECK(m.segment_offset == expected_bank * 0x2000u + (addr & 0x1FFFu));
        }
    }

    for(unsigned mode = 0; mode < 2; mode++) {
        Ai2Mmc3State s;
        ai2_mmc3_power_on_unknown(&s);
        unsigned bits = mode ? 0x80u : 0u;
        for(unsigned r = 0; r < 6; r++) {
            if(write_reg(&s, r, bits, (uint8_t)(r * 10u + 2u))) return 1;
        }
        for(unsigned addr = 0; addr < 0x2000u; addr++) {
            unsigned slot = addr >> 10;
            unsigned pair = 0;
            unsigned reg = expected_chr_reg(mode, slot, &pair);
            uint8_t raw = (uint8_t)(reg * 10u + 2u);
            if(reg <= 1u) raw &= 0xFEu;
            unsigned expected_bank = (raw + pair) & 0x7Fu;
            Ai2MapResult m = ai2_mmc3_map_ppu_read(&s, (uint16_t)addr);
            CHECK(m.kind == AI2_MAP_CHR_ROM);
            CHECK(m.physical_bank == expected_bank);
            CHECK(m.bank_offset == (addr & 0x03FFu));
            CHECK(m.segment_offset == expected_bank * 0x400u + (addr & 0x03FFu));
        }
    }
    return 0;
}


static int test_full_read_address_space(void)
{
    Ai2Mmc3State s;
    ai2_mmc3_power_on_unknown(&s);
    /* Establish every mapping latch used by CPU/PPU reads. */
    if(write_reg(&s, 0, 0, 0x22u)) return 1;
    if(write_reg(&s, 1, 0, 0x44u)) return 1;
    if(write_reg(&s, 2, 0, 0x66u)) return 1;
    if(write_reg(&s, 3, 0, 0x77u)) return 1;
    if(write_reg(&s, 4, 0, 0x12u)) return 1;
    if(write_reg(&s, 5, 0, 0x34u)) return 1;
    if(write_reg(&s, 6, 0, 0x03u)) return 1;
    if(write_reg(&s, 7, 0, 0x05u)) return 1;
    CHECK(ai2_mmc3_cpu_write(&s, 0xA000u, 1u) == AI2_MAPPER_WRITE_APPLIED);

    for(unsigned addr = 0; addr <= 0xFFFFu; addr++) {
        Ai2MapResult m = ai2_mmc3_map_cpu_read(&s, (uint16_t)addr);
        if(addr < 0x4020u) {
            CHECK(m.kind == AI2_MAP_NOT_CARTRIDGE);
        } else if(addr < 0x8000u) {
            CHECK(m.kind == AI2_MAP_OPEN_BUS);
        } else {
            unsigned expected_bank;
            if(addr < 0xA000u) expected_bank = 3u;
            else if(addr < 0xC000u) expected_bank = 5u;
            else if(addr < 0xE000u) expected_bank = 14u;
            else expected_bank = 15u;
            CHECK(m.kind == AI2_MAP_PRG_ROM);
            CHECK(m.physical_bank == expected_bank);
            CHECK(m.bank_offset == (addr & 0x1FFFu));
            CHECK(m.segment_offset == expected_bank * 0x2000u + (addr & 0x1FFFu));
        }
    }

    for(unsigned addr = 0; addr < 0x4000u; addr++) {
        Ai2MapResult m = ai2_mmc3_map_ppu_read(&s, (uint16_t)addr);
        if(addr < 0x2000u) {
            CHECK(m.kind == AI2_MAP_CHR_ROM);
            CHECK(m.physical_bank < AI2_CHR_BANK_COUNT);
            CHECK(m.bank_offset == (addr & 0x03FFu));
        } else if(addr < 0x3F00u) {
            CHECK(m.kind == AI2_MAP_CIRAM);
            CHECK(m.ciram_page <= 1u);
            CHECK(m.bank_offset < 0x400u);
            CHECK(m.segment_offset < 0x800u);
        } else {
            CHECK(m.kind == AI2_MAP_NOT_CARTRIDGE);
        }
    }
    return 0;
}

static int test_irq_counter_state_space(void)
{
    for(unsigned latch = 0; latch < 256u; latch++) {
        for(unsigned counter = 0; counter < 256u; counter++) {
            for(unsigned reload = 0; reload < 2u; reload++) {
                for(unsigned enabled = 0; enabled < 2u; enabled++) {
                    Ai2Mmc3State s;
                    ai2_mmc3_power_on_unknown(&s);
                    s.irq_latch = (uint8_t)latch;
                    s.irq_counter = (uint8_t)counter;
                    s.irq_latch_known = true;
                    s.irq_counter_known = true;
                    s.irq_reload_pending = reload != 0;
                    s.irq_reload_known = true;
                    s.irq_enabled = enabled != 0;
                    s.irq_enabled_known = true;
                    s.irq_pending = false;
                    s.irq_pending_known = true;

                    uint8_t expected = (counter == 0u || reload)
                        ? (uint8_t)latch
                        : (uint8_t)(counter - 1u);
                    bool expected_pending = enabled && expected == 0u;
                    CHECK(ai2_mmc3_clock_irq_counter(&s) == AI2_IRQ_CLOCKED);
                    CHECK(s.irq_counter == expected);
                    CHECK(!s.irq_reload_pending && s.irq_reload_known);
                    CHECK(s.irq_pending == expected_pending && s.irq_pending_known);
                }
            }
        }
    }
    return 0;
}

static int test_write_decode_and_ram_absence(void)
{
    const uint16_t bank_select_addrs[] = {0x8000u,0x8002u,0x9FFEu};
    const uint16_t bank_data_addrs[] = {0x8001u,0x8003u,0x9FFFu};
    const uint16_t mirroring_addrs[] = {0xA000u,0xA222u,0xBFFEu};
    const uint16_t ram_addrs[] = {0xA001u,0xA223u,0xBFFFu};
    const uint16_t latch_addrs[] = {0xC000u,0xC222u,0xDFFEu};
    const uint16_t reload_addrs[] = {0xC001u,0xC223u,0xDFFFu};
    const uint16_t disable_addrs[] = {0xE000u,0xE222u,0xFFFEu};
    const uint16_t enable_addrs[] = {0xE001u,0xE223u,0xFFFFu};

    for(unsigned i = 0; i < 3; i++) {
        Ai2Mmc3State s;
        ai2_mmc3_power_on_unknown(&s);
        CHECK(ai2_mmc3_cpu_write(&s, bank_select_addrs[i], 0xC6u) == AI2_MAPPER_WRITE_APPLIED);
        CHECK(s.bank_select_known && s.bank_select == 0xC6u);
        CHECK(ai2_mmc3_cpu_write(&s, bank_data_addrs[i], 0xFFu) == AI2_MAPPER_WRITE_APPLIED);
        CHECK((s.bank_reg_known_mask & (1u << 6)) != 0);
        CHECK(s.bank_regs[6] == 0x3Fu);

        CHECK(ai2_mmc3_cpu_write(&s, mirroring_addrs[i], 1u) == AI2_MAPPER_WRITE_APPLIED);
        CHECK(s.mirroring_known && s.mirroring == AI2_MIRROR_HORIZONTAL);
        CHECK(ai2_mmc3_cpu_write(&s, ram_addrs[i], 0xC0u) == AI2_MAPPER_WRITE_APPLIED);
        CHECK(s.ram_control_known && s.ram_control == 0xC0u);
        CHECK(ai2_mmc3_map_cpu_read(&s, 0x6000u).kind == AI2_MAP_OPEN_BUS);
        CHECK(ai2_mmc3_map_cpu_read(&s, 0x7FFFu).kind == AI2_MAP_OPEN_BUS);

        CHECK(ai2_mmc3_cpu_write(&s, latch_addrs[i], 7u) == AI2_MAPPER_WRITE_APPLIED);
        CHECK(s.irq_latch_known && s.irq_latch == 7u);
        CHECK(ai2_mmc3_cpu_write(&s, reload_addrs[i], 0xAAu) == AI2_MAPPER_WRITE_APPLIED);
        CHECK(s.irq_counter_known && s.irq_counter == 0u && s.irq_reload_pending);
        CHECK(ai2_mmc3_cpu_write(&s, enable_addrs[i], 0u) == AI2_MAPPER_WRITE_APPLIED);
        CHECK(s.irq_enabled_known && s.irq_enabled);
        s.irq_pending = true; s.irq_pending_known = true;
        CHECK(ai2_mmc3_cpu_write(&s, disable_addrs[i], 0u) == AI2_MAPPER_WRITE_APPLIED);
        CHECK(!s.irq_enabled && !s.irq_pending);
    }

    /* R0/R1 ignore D0; R6/R7 ignore D7-D6. */
    Ai2Mmc3State s;
    ai2_mmc3_power_on_unknown(&s);
    if(write_reg(&s, 0, 0, 0xFFu)) return 1;
    if(write_reg(&s, 1, 0, 0x55u)) return 1;
    if(write_reg(&s, 6, 0, 0xFFu)) return 1;
    if(write_reg(&s, 7, 0, 0xC5u)) return 1;
    CHECK(s.bank_regs[0] == 0xFEu);
    CHECK(s.bank_regs[1] == 0x54u);
    CHECK(s.bank_regs[6] == 0x3Fu);
    CHECK(s.bank_regs[7] == 0x05u);
    return 0;
}

static void qualified_a12_edge(Ai2Mmc3State *s)
{
    (void)ai2_mmc3_observe_ppu_address(s, 0x0000u);
    ai2_mmc3_clock_m2_falling_edge(s);
    ai2_mmc3_clock_m2_falling_edge(s);
    ai2_mmc3_clock_m2_falling_edge(s);
    (void)ai2_mmc3_observe_ppu_address(s, 0x1000u);
}

static int test_irq_and_a12_filter(void)
{
    Ai2Mmc3State s;
    ai2_mmc3_power_on_unknown(&s);
    CHECK(ai2_mmc3_clock_irq_counter(&s) == AI2_IRQ_CLOCK_AMBIGUOUS_POWER_STATE);

    CHECK(ai2_mmc3_cpu_write(&s, 0xC000u, 2u) == AI2_MAPPER_WRITE_APPLIED);
    CHECK(ai2_mmc3_cpu_write(&s, 0xC001u, 0u) == AI2_MAPPER_WRITE_APPLIED);
    CHECK(ai2_mmc3_cpu_write(&s, 0xE001u, 0u) == AI2_MAPPER_WRITE_APPLIED);

    CHECK(ai2_mmc3_observe_ppu_address(&s, 0x0000u) == AI2_IRQ_NO_CLOCK);
    ai2_mmc3_clock_m2_falling_edge(&s);
    ai2_mmc3_clock_m2_falling_edge(&s);
    CHECK(ai2_mmc3_observe_ppu_address(&s, 0x1000u) == AI2_IRQ_NO_CLOCK);
    CHECK(s.irq_counter == 0u && s.irq_reload_pending);

    qualified_a12_edge(&s);
    CHECK(s.irq_counter == 2u && !s.irq_reload_pending && !s.irq_pending);
    qualified_a12_edge(&s);
    CHECK(s.irq_counter == 1u && !s.irq_pending);
    qualified_a12_edge(&s);
    CHECK(s.irq_counter == 0u && s.irq_pending);

    CHECK(ai2_mmc3_cpu_write(&s, 0xE000u, 0u) == AI2_MAPPER_WRITE_APPLIED);
    CHECK(!s.irq_pending && !s.irq_enabled);

    /* Sharp MMC3B behavior: latch 0 can assert on every qualified edge. */
    CHECK(ai2_mmc3_cpu_write(&s, 0xC000u, 0u) == AI2_MAPPER_WRITE_APPLIED);
    CHECK(ai2_mmc3_cpu_write(&s, 0xC001u, 0u) == AI2_MAPPER_WRITE_APPLIED);
    CHECK(ai2_mmc3_cpu_write(&s, 0xE001u, 0u) == AI2_MAPPER_WRITE_APPLIED);
    qualified_a12_edge(&s);
    CHECK(s.irq_counter == 0u && s.irq_pending);
    CHECK(ai2_mmc3_cpu_write(&s, 0xE000u, 0u) == AI2_MAPPER_WRITE_APPLIED);
    CHECK(ai2_mmc3_cpu_write(&s, 0xE001u, 0u) == AI2_MAPPER_WRITE_APPLIED);
    qualified_a12_edge(&s);
    CHECK(s.irq_counter == 0u && s.irq_pending);
    return 0;
}

static int test_exact_rom_vectors(const char *rom_path)
{
    FILE *f = fopen(rom_path, "rb");
    CHECK(f != NULL);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    long size = ftell(f);
    CHECK(size == 262160L);
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    unsigned char *data = (unsigned char *)malloc((size_t)size);
    CHECK(data != NULL);
    CHECK(fread(data, 1, (size_t)size, f) == (size_t)size);
    CHECK(fclose(f) == 0);
    CHECK(memcmp(data, "NES\x1A", 4) == 0);

    const uint16_t addrs[6] = {0xFFFAu,0xFFFBu,0xFFFCu,0xFFFDu,0xFFFEu,0xFFFFu};
    const unsigned char expected[6] = {0xCEu,0xE0u,0xAEu,0xFFu,0x52u,0xE1u};
    Ai2Mmc3State s;
    ai2_mmc3_power_on_unknown(&s);
    for(unsigned i = 0; i < 6; i++) {
        Ai2MapResult m = ai2_mmc3_map_cpu_read(&s, addrs[i]);
        CHECK(m.kind == AI2_MAP_PRG_ROM);
        CHECK(m.physical_bank == 15u);
        CHECK(16u + m.segment_offset < (unsigned long)size);
        CHECK(data[16u + m.segment_offset] == expected[i]);
    }
    free(data);
    return 0;
}

int main(int argc, char **argv)
{
    if(test_power_and_reset()) return 1;
    if(test_prg_mapping()) return 1;
    if(test_chr_mapping()) return 1;
    if(test_nametable_mapping()) return 1;
    if(test_all_address_decoding()) return 1;
    if(test_full_read_address_space()) return 1;
    if(test_write_decode_and_ram_absence()) return 1;
    if(test_irq_and_a12_filter()) return 1;
    if(test_irq_counter_state_space()) return 1;
    if(argc > 1 && test_exact_rom_vectors(argv[1])) return 1;

    printf("PASS: V02 TLROM-03/Sharp MMC3B mapper contract (%u checks)%s\n",
           checks, argc > 1 ? " with exact-ROM vector integration" : "");
    return 0;
}
