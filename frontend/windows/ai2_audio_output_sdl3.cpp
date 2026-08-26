#include "ai2_audio_output_sdl3.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <iterator>

namespace { constexpr int kSampleRate=44100; }

static void set_error(wchar_t *error,size_t cap,const wchar_t *prefix){
    wchar_t detail[256]={};if(!error||!cap)return;
    MultiByteToWideChar(CP_UTF8,0,SDL_GetError(),-1,detail,(int)std::size(detail));
    _snwprintf(error,cap,L"%s: %s",prefix,detail[0]?detail:L"Unknown SDL audio error");error[cap-1]=0;
}
void ai2_audio_output_initialize(AI2AudioOutput *o){if(o)std::memset(o,0,sizeof(*o));}
int ai2_audio_output_open(AI2AudioOutput *o,int volume,int latency,wchar_t *error,size_t cap){
    if(!o)return 0;ai2_audio_output_close(o);
    if(!SDL_InitSubSystem(SDL_INIT_AUDIO)){set_error(error,cap,L"Unable to initialize SDL audio");return 0;}o->initialized=1;
    SDL_AudioSpec spec{};spec.format=SDL_AUDIO_S16;spec.channels=1;spec.freq=kSampleRate;
    o->stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
    if(!o->stream){set_error(error,cap,L"Unable to open audio output");ai2_audio_output_close(o);return 0;}
    volume=std::clamp(volume,0,100);latency=std::clamp(latency,20,250);
    SDL_SetAudioStreamGain(o->stream,(float)volume/100.0f);o->target_frames=(uint32_t)((uint64_t)kSampleRate*latency/1000u);
    o->playback_ratio=1.0f;o->paused=1;o->priming=1;return 1;
}
void ai2_audio_output_flush(AI2AudioOutput *o){if(o&&o->stream){SDL_ClearAudioStream(o->stream);o->priming=1;}}
void ai2_audio_output_close(AI2AudioOutput *o){if(!o)return;if(o->stream)SDL_DestroyAudioStream(o->stream);if(o->initialized)SDL_QuitSubSystem(SDL_INIT_AUDIO);std::memset(o,0,sizeof(*o));}
void ai2_audio_output_pause(AI2AudioOutput *o){if(!o||!o->stream||o->paused)return;SDL_PauseAudioStreamDevice(o->stream);ai2_audio_output_flush(o);o->paused=1;}
void ai2_audio_output_resume(AI2AudioOutput *o){if(!o||!o->stream||!o->paused)return;ai2_audio_output_flush(o);o->paused=0;}
void ai2_audio_output_push(AI2AudioOutput *o,const int16_t *samples,size_t count){
    if(!o||!o->stream||!samples||!count)return;
    SDL_PutAudioStreamData(o->stream,samples,(int)(count*sizeof(int16_t)));
    int queued=SDL_GetAudioStreamQueued(o->stream);uint32_t frames=queued>0?(uint32_t)queued/sizeof(int16_t):0u;
    if(o->priming&&!o->paused&&frames>=o->target_frames/2u){if(SDL_ResumeAudioStreamDevice(o->stream))o->priming=0;}
    if(!o->priming&&!o->paused){uint32_t tolerance=kSampleRate*3u/1000u;float wanted=1.0f;
        if(frames>o->target_frames+tolerance)wanted=1.0025f;else if(frames+tolerance<o->target_frames)wanted=0.9975f;
        o->playback_ratio+=(wanted-o->playback_ratio)*0.05f;SDL_SetAudioStreamFrequencyRatio(o->stream,o->playback_ratio);}
}
