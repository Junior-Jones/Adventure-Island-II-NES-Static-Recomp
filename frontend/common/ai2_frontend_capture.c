#include "ai2_frontend_capture.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

uint64_t ai2_frontend_frame_hash_bgra(const uint32_t *pixels, size_t count)
{
    uint64_t hash = 1469598103934665603ull;
    size_t i;
    unsigned j;
    if(pixels == NULL) return 0u;
    for(i = 0; i < count; i++) {
        uint32_t value = pixels[i];
        for(j = 0; j < 4u; j++) {
            hash ^= (uint8_t)(value >> (8u * j));
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

int ai2_frontend_write_bmp(const char *path, const uint32_t *pixels, unsigned width, unsigned height)
{
    FILE *file;
    uint8_t header[54] = {0};
    unsigned y, x;
    uint32_t row_bytes = width * 3u;
    uint32_t padding = (4u - (row_bytes & 3u)) & 3u;
    uint32_t image_bytes = (row_bytes + padding) * height;
    uint32_t total_bytes = 54u + image_bytes;
    uint8_t zero[3] = {0, 0, 0};

    if(path == NULL || pixels == NULL || width == 0u || height == 0u) return 0;
    file = fopen(path, "wb");
    if(file == NULL) return 0;

    header[0] = 'B';
    header[1] = 'M';
    put32(header + 2, total_bytes);
    put32(header + 10, 54u);
    put32(header + 14, 40u);
    put32(header + 18, width);
    put32(header + 22, height);
    put16(header + 26, 1u);
    put16(header + 28, 24u);
    put32(header + 34, image_bytes);
    if(fwrite(header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        return 0;
    }

    for(y = height; y > 0u; y--) {
        const uint32_t *row = pixels + (size_t)(y - 1u) * width;
        for(x = 0; x < width; x++) {
            uint32_t color = row[x];
            uint8_t bgr[3] = {(uint8_t)color, (uint8_t)(color >> 8), (uint8_t)(color >> 16)};
            if(fwrite(bgr, 1, 3, file) != 3u) {
                fclose(file);
                return 0;
            }
        }
        if(padding != 0u && fwrite(zero, 1, padding, file) != padding) {
            fclose(file);
            return 0;
        }
    }
    return fclose(file) == 0;
}

int ai2_wav_open(Ai2WavWriter *writer, const char *path, uint32_t rate)
{
    uint8_t header[44] = {0};
    if(writer == NULL || path == NULL || rate == 0u) return 0;
    memset(writer, 0, sizeof(*writer));
    writer->sample_rate = rate;
    writer->file = fopen(path, "wb");
    if(writer->file == NULL) return 0;

    memcpy(header, "RIFF", 4);
    put32(header + 4, 36u);
    memcpy(header + 8, "WAVEfmt ", 8);
    put32(header + 16, 16u);
    put16(header + 20, 1u);
    put16(header + 22, 1u);
    put32(header + 24, rate);
    put32(header + 28, rate * 2u);
    put16(header + 32, 2u);
    put16(header + 34, 16u);
    memcpy(header + 36, "data", 4);
    put32(header + 40, 0u);
    if(fwrite(header, 1, sizeof(header), writer->file) != sizeof(header)) {
        fclose(writer->file);
        memset(writer, 0, sizeof(*writer));
        return 0;
    }
    return 1;
}

int ai2_wav_append(Ai2WavWriter *writer, const int16_t *samples, size_t count)
{
    if(writer == NULL || writer->file == NULL || (samples == NULL && count != 0u) || writer->failed) return 0;
    if(count != 0u && fwrite(samples, sizeof(*samples), count, writer->file) != count) {
        writer->failed = 1;
        return 0;
    }
    writer->samples += count;
    return 1;
}

int ai2_wav_close(Ai2WavWriter *writer)
{
    uint64_t data_bytes;
    uint8_t value[4];
    int ok = 1;
    if(writer == NULL) return 0;
    if(writer->file == NULL) return !writer->failed;

    data_bytes = writer->samples * 2ull;
    if(data_bytes > 0xFFFFFFFFull) {
        writer->failed = 1;
        ok = 0;
    }
    if(ok && fseek(writer->file, 4, SEEK_SET) == 0) {
        put32(value, (uint32_t)(36ull + data_bytes));
        if(fwrite(value, 1, 4, writer->file) != 4u) ok = 0;
    } else {
        ok = 0;
    }
    if(ok && fseek(writer->file, 40, SEEK_SET) == 0) {
        put32(value, (uint32_t)data_bytes);
        if(fwrite(value, 1, 4, writer->file) != 4u) ok = 0;
    } else {
        ok = 0;
    }
    if(fclose(writer->file) != 0) ok = 0;
    writer->file = NULL;
    return ok && !writer->failed;
}
