#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AI2Rom {
    uint8_t *file_data;
    size_t file_size;
} AI2Rom;

int ai2_rom_load(const char *path, AI2Rom *rom, char *error, size_t error_capacity);
void ai2_rom_free(AI2Rom *rom);
int ai2_rom_is_expected(const AI2Rom *rom, char *reason, size_t reason_capacity);

#ifdef __cplusplus
}
#endif
