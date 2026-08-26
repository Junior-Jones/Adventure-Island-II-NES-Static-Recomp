#ifndef AI2_FRONTEND_CAPTURE_H
#define AI2_FRONTEND_CAPTURE_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Ai2WavWriter {
    FILE *file;
    uint64_t samples;
    uint32_t sample_rate;
    int failed;
} Ai2WavWriter;

uint64_t ai2_frontend_frame_hash_bgra(const uint32_t *pixels,size_t count);
int ai2_frontend_write_bmp(const char *path,const uint32_t *pixels,unsigned width,unsigned height);
int ai2_wav_open(Ai2WavWriter *writer,const char *path,uint32_t sample_rate);
int ai2_wav_append(Ai2WavWriter *writer,const int16_t *samples,size_t count);
int ai2_wav_close(Ai2WavWriter *writer);
#ifdef __cplusplus
}
#endif
#endif
