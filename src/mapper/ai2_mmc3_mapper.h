#ifndef AI2_MMC3_MAPPER_H
#define AI2_MMC3_MAPPER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exact Adventure Island II (USA) cartridge geometry. */
enum {
    AI2_PRG_ROM_BYTES = 128 * 1024,
    AI2_CHR_ROM_BYTES = 128 * 1024,
    AI2_PRG_BANK_BYTES = 8 * 1024,
    AI2_CHR_BANK_BYTES = 1 * 1024,
    AI2_PRG_BANK_COUNT = 16,
    AI2_CHR_BANK_COUNT = 128,
    AI2_FIXED_PRG_SECOND_LAST = 14,
    AI2_FIXED_PRG_LAST = 15,
    AI2_MMC3_A12_LOW_M2_FALLS = 3
};

typedef enum Ai2MapKind {
    AI2_MAP_NOT_CARTRIDGE = 0,
    AI2_MAP_OPEN_BUS,
    AI2_MAP_PRG_ROM,
    AI2_MAP_CHR_ROM,
    AI2_MAP_CIRAM,
    AI2_MAP_UNRESOLVED_POWER_STATE
} Ai2MapKind;

typedef enum Ai2NametableMirroring {
    /* MMC3 $A000 bit 0: 0 = CIRAM A10 <- PPU A10 = vertical mirroring. */
    AI2_MIRROR_VERTICAL = 0,
    /* MMC3 $A000 bit 0: 1 = CIRAM A10 <- PPU A11 = horizontal mirroring. */
    AI2_MIRROR_HORIZONTAL = 1
} Ai2NametableMirroring;

typedef enum Ai2MapperWriteResult {
    AI2_MAPPER_WRITE_NOT_MAPPER = 0,
    AI2_MAPPER_WRITE_APPLIED,
    AI2_MAPPER_WRITE_AMBIGUOUS_POWER_STATE
} Ai2MapperWriteResult;

typedef enum Ai2Mmc3Register {
    AI2_MMC3_REG_NONE = 0,
    AI2_MMC3_REG_BANK_SELECT,
    AI2_MMC3_REG_BANK_DATA,
    AI2_MMC3_REG_MIRRORING,
    AI2_MMC3_REG_RAM_PROTECT,
    AI2_MMC3_REG_IRQ_LATCH,
    AI2_MMC3_REG_IRQ_RELOAD,
    AI2_MMC3_REG_IRQ_DISABLE,
    AI2_MMC3_REG_IRQ_ENABLE
} Ai2Mmc3Register;

typedef enum Ai2IrqClockResult {
    AI2_IRQ_NO_CLOCK = 0,
    AI2_IRQ_CLOCKED,
    AI2_IRQ_CLOCK_AMBIGUOUS_POWER_STATE
} Ai2IrqClockResult;

typedef struct Ai2MapResult {
    Ai2MapKind kind;
    uint16_t physical_bank;
    uint16_t bank_offset;
    uint32_t segment_offset;
    uint8_t ciram_page;
} Ai2MapResult;

typedef struct Ai2Mmc3State {
    uint8_t bank_select;
    uint8_t bank_regs[8];
    uint8_t bank_reg_known_mask;
    bool bank_select_known;

    uint8_t ram_control;
    bool ram_control_known;

    Ai2NametableMirroring mirroring;
    bool mirroring_known;

    uint8_t irq_latch;
    uint8_t irq_counter;
    bool irq_latch_known;
    bool irq_counter_known;
    bool irq_reload_pending;
    bool irq_reload_known;
    bool irq_enabled;
    bool irq_enabled_known;
    bool irq_pending;
    bool irq_pending_known;

    bool ppu_a12_high;
    bool ppu_a12_known;
    uint8_t a12_low_m2_falls;
} Ai2Mmc3State;

/*
 * The real Sharp MMC3B does not provide useful guaranteed defaults for the
 * switchable bank state at power-on. This constructor marks those values
 * unknown. Fixed final PRG bank mapping is still resolvable immediately.
 */
void ai2_mmc3_power_on_unknown(Ai2Mmc3State *state);

/* NES reset does not provide a dedicated reset signal to this MMC3 ASIC. */
void ai2_mmc3_cpu_reset(Ai2Mmc3State *state);

/* CPU reads route through this one board mapping authority. */
Ai2MapResult ai2_mmc3_map_cpu_read(const Ai2Mmc3State *state, uint16_t address);

/* PPU pattern/nametable reads route through this one board mapping authority. */
Ai2MapResult ai2_mmc3_map_ppu_read(const Ai2Mmc3State *state, uint16_t address);

/* Canonical decode for every CPU write address. */
Ai2Mmc3Register ai2_mmc3_decode_cpu_write(uint16_t address);

/* Decode and apply MMC3 writes at $8000-$FFFF. */
Ai2MapperWriteResult ai2_mmc3_cpu_write(Ai2Mmc3State *state, uint16_t address, uint8_t value);

/* Feed M2 falling edges while the last observed PPU A12 level is low. */
void ai2_mmc3_clock_m2_falling_edge(Ai2Mmc3State *state);

/* Observe a PPU address and clock the Sharp MMC3B IRQ unit on qualified A12 rises. */
Ai2IrqClockResult ai2_mmc3_observe_ppu_address(Ai2Mmc3State *state, uint16_t ppu_address);

/* Direct qualified-edge entry point used by deterministic construction tests. */
Ai2IrqClockResult ai2_mmc3_clock_irq_counter(Ai2Mmc3State *state);

/* Exposed normalization helpers define physical-bank numbering exactly. */
uint8_t ai2_mmc3_physical_prg_bank(uint8_t mmc3_prg_value);
uint8_t ai2_mmc3_physical_chr_bank(uint8_t mmc3_chr_value);

#ifdef __cplusplus
}
#endif

#endif
