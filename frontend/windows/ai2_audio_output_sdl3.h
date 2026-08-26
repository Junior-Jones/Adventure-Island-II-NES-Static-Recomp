#ifndef AI2_AUDIO_OUTPUT_SDL3_H
#define AI2_AUDIO_OUTPUT_SDL3_H

#include <windows.h>
#include <stdint.h>
#include <SDL3/SDL_audio.h>

typedef struct AI2AudioOutput {
    SDL_AudioStream *stream;
    uint32_t target_frames;
    float playback_ratio;
    int initialized;
    int paused;
    int priming;
} AI2AudioOutput;

void ai2_audio_output_initialize(AI2AudioOutput *output);
int ai2_audio_output_open(AI2AudioOutput *output,int volume_percent,int latency_ms,
    wchar_t *error,size_t error_capacity);
void ai2_audio_output_close(AI2AudioOutput *output);
void ai2_audio_output_pause(AI2AudioOutput *output);
void ai2_audio_output_resume(AI2AudioOutput *output);
void ai2_audio_output_flush(AI2AudioOutput *output);
void ai2_audio_output_push(AI2AudioOutput *output,const int16_t *samples,size_t count);

#endif
