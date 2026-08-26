#ifndef AI2_LINUX_AUDIO_H
#define AI2_LINUX_AUDIO_H
#include <stddef.h>
#include <stdint.h>

typedef int (*Ai2LinuxAudioWriteFn)(void *, const void *, size_t, int *);
typedef int (*Ai2LinuxAudioDrainFn)(void *, int *);
typedef void (*Ai2LinuxAudioFreeFn)(void *);

typedef struct Ai2LinuxAudio {
    void *library;
    void *stream;
    int available;
    Ai2LinuxAudioWriteFn fn_write;
    Ai2LinuxAudioDrainFn fn_drain;
    Ai2LinuxAudioFreeFn fn_free;
} Ai2LinuxAudio;

int ai2_linux_audio_open(Ai2LinuxAudio *audio, uint32_t sample_rate);
void ai2_linux_audio_write(Ai2LinuxAudio *audio, const int16_t *samples, size_t count);
void ai2_linux_audio_close(Ai2LinuxAudio *audio);
#endif
