#ifndef AI2_V07_PPU_H
#define AI2_V07_PPU_H

#include <stddef.h>
#include <stdint.h>

#include "ai2_v06_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    AI2_V07_SCREEN_WIDTH = 256,
    AI2_V07_SCREEN_HEIGHT = 240,
    AI2_V07_SCREEN_PIXELS = AI2_V07_SCREEN_WIDTH * AI2_V07_SCREEN_HEIGHT,
    AI2_V07_CIRAM_BYTES = 0x800,
    AI2_V07_PALETTE_BYTES = 0x20,
    AI2_V07_OAM_BYTES = 0x100,
    AI2_V07_SECONDARY_OAM_BYTES = 0x20,
    AI2_V07_SPRITES_PER_LINE = 8,
    AI2_V07_NTSC_SCANLINES = 262,
    AI2_V07_DOTS_PER_SCANLINE = 341,
    AI2_V07_STATUS_OVERFLOW = 0x20,
    AI2_V07_STATUS_SPRITE0 = 0x40,
    AI2_V07_STATUS_VBLANK = 0x80
};

typedef enum Ai2V07PpuError {
    AI2_V07_PPU_OK = 0,
    AI2_V07_PPU_ERR_NULL,
    AI2_V07_PPU_ERR_CHR_GEOMETRY,
    AI2_V07_PPU_ERR_MAPPER_UNRESOLVED,
    AI2_V07_PPU_ERR_BAD_MAPPING,
    AI2_V07_PPU_ERR_FRAME_BUFFER
} Ai2V07PpuError;

typedef struct Ai2V07Sprite {
    uint8_t y;
    uint8_t tile;
    uint8_t attr;
    uint8_t x;
    uint8_t pattern_lo;
    uint8_t pattern_hi;
    uint8_t oam_index;
    uint8_t valid;
    uint8_t row;
} Ai2V07Sprite;

typedef struct Ai2V07Ppu {
    uint8_t ctrl;
    uint8_t mask;
    uint8_t status;
    uint8_t oam_addr;

    uint16_t v;
    uint16_t t;
    uint8_t fine_x;
    uint8_t write_toggle;
    uint8_t read_buffer;
    uint8_t ppu_open_bus;

    uint8_t ciram[AI2_V07_CIRAM_BYTES];
    uint8_t palette[AI2_V07_PALETTE_BYTES];
    uint8_t oam[AI2_V07_OAM_BYTES];
    uint8_t secondary_oam[AI2_V07_SECONDARY_OAM_BYTES];

    uint16_t bg_shift_lo;
    uint16_t bg_shift_hi;
    uint16_t at_shift_lo;
    uint16_t at_shift_hi;
    uint8_t next_tile;
    uint8_t next_attr;
    uint8_t next_bg_lo;
    uint8_t next_bg_hi;

    Ai2V07Sprite sprites[AI2_V07_SPRITES_PER_LINE];
    Ai2V07Sprite next_sprites[AI2_V07_SPRITES_PER_LINE];
    uint8_t sprite_count;
    uint8_t next_sprite_count;
    uint8_t sprite_zero_current;
    uint8_t sprite_zero_next;

    uint16_t frame_indexed[AI2_V07_SCREEN_PIXELS];
    uint64_t frame_generation;
    uint64_t frame_count;

    int16_t scanline; /* 0-239 visible, 240 post-render, 241-260 vblank, 261 pre-render */
    uint16_t dot;     /* 0-340 */
    uint8_t suppress_vblank;
    uint8_t nmi_line;

    uint16_t last_bus_address;
    uint64_t ppu_memory_reads;
    uint64_t ppu_memory_writes;
    uint64_t mapper_a12_clocks;
    uint64_t mapper_a12_ambiguous_while_irq_disabled;
    Ai2V07PpuError error;
} Ai2V07Ppu;

void ai2_v07_ppu_init(Ai2V07Ppu *ppu, uint8_t ciram_fill, uint8_t oam_fill);
void ai2_v07_ppu_bind(Ai2V05Runtime *rt, Ai2V07Ppu *ppu);

int ai2_v07_ppu_cpu_read(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t *value);
int ai2_v07_ppu_cpu_write(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t value);
void ai2_v07_ppu_tick(Ai2V05Runtime *rt, void *user);

int ai2_v07_ppu_memory_read(Ai2V05Runtime *rt, Ai2V07Ppu *ppu, uint16_t address, uint8_t *value);
int ai2_v07_ppu_memory_write(Ai2V05Runtime *rt, Ai2V07Ppu *ppu, uint16_t address, uint8_t value);

int ai2_v07_ppu_copy_indexed(const Ai2V07Ppu *ppu, uint16_t *dst, size_t pixels, uint64_t *generation);
int ai2_v07_ppu_copy_bgra(const Ai2V07Ppu *ppu, uint32_t *dst, size_t pixels, uint64_t *generation);

int ai2_v07_ppu_rendering_enabled(const Ai2V07Ppu *ppu);
Ai2V07PpuError ai2_v07_ppu_error(const Ai2V07Ppu *ppu);

#ifdef __cplusplus
}
#endif

#endif
