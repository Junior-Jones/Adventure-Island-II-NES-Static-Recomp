#include "ai2_linux_audio.h"
#include <dlfcn.h>
#include <string.h>

typedef struct pa_simple pa_simple;
typedef struct {
    int format;
    uint32_t rate;
    uint8_t channels;
} pa_sample_spec;
typedef pa_simple *(*PaSimpleNewFn)(const char *, const char *, int, const char *, const char *, const pa_sample_spec *, const void *, const void *, int *);

static void load_symbol(void *library, const char *name, void *target, size_t target_size)
{
    void *symbol = dlsym(library, name);
    memset(target, 0, target_size);
    memcpy(target, &symbol, target_size < sizeof(symbol) ? target_size : sizeof(symbol));
}

int ai2_linux_audio_open(Ai2LinuxAudio *audio, uint32_t rate)
{
    PaSimpleNewFn create = NULL;
    pa_sample_spec spec;
    int error = 0;
    if(audio == NULL) return 0;
    memset(audio, 0, sizeof(*audio));
    audio->library = dlopen("libpulse-simple.so.0", RTLD_NOW | RTLD_LOCAL);
    if(audio->library == NULL) return 0;

    load_symbol(audio->library, "pa_simple_new", &create, sizeof(create));
    load_symbol(audio->library, "pa_simple_write", &audio->fn_write, sizeof(audio->fn_write));
    load_symbol(audio->library, "pa_simple_drain", &audio->fn_drain, sizeof(audio->fn_drain));
    load_symbol(audio->library, "pa_simple_free", &audio->fn_free, sizeof(audio->fn_free));
    if(create == NULL || audio->fn_write == NULL || audio->fn_drain == NULL || audio->fn_free == NULL) {
        dlclose(audio->library);
        memset(audio, 0, sizeof(*audio));
        return 0;
    }

    spec.format = 3; /* PA_SAMPLE_S16LE */
    spec.rate = rate;
    spec.channels = 1;
    audio->stream = create(NULL, "Adventure Island II", 1, NULL, "NES audio", &spec, NULL, NULL, &error);
    if(audio->stream == NULL) {
        dlclose(audio->library);
        memset(audio, 0, sizeof(*audio));
        return 0;
    }
    audio->available = 1;
    return 1;
}

void ai2_linux_audio_write(Ai2LinuxAudio *audio, const int16_t *samples, size_t count)
{
    int error = 0;
    if(audio != NULL && audio->available && count != 0u) {
        (void)audio->fn_write(audio->stream, samples, count * sizeof(*samples), &error);
    }
}

void ai2_linux_audio_close(Ai2LinuxAudio *audio)
{
    int error = 0;
    if(audio == NULL) return;
    if(audio->stream != NULL) {
        (void)audio->fn_drain(audio->stream, &error);
        audio->fn_free(audio->stream);
    }
    if(audio->library != NULL) dlclose(audio->library);
    memset(audio, 0, sizeof(*audio));
}
