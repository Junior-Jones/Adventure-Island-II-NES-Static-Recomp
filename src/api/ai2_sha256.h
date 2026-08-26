#ifndef AI2_SHA256_H
#define AI2_SHA256_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct Ai2Sha256Ctx {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t buffer[64];
    size_t buffer_used;
} Ai2Sha256Ctx;
void ai2_sha256_init(Ai2Sha256Ctx *ctx);
void ai2_sha256_update(Ai2Sha256Ctx *ctx, const void *data, size_t size);
void ai2_sha256_final(Ai2Sha256Ctx *ctx, uint8_t digest[32]);
void ai2_sha256(const void *data, size_t size, uint8_t digest[32]);
#ifdef __cplusplus
}
#endif
#endif
