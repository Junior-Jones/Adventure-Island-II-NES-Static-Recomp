#ifndef AI2_VIDEO_OUTPUT_SDL3_H
#define AI2_VIDEO_OUTPUT_SDL3_H

#include <windows.h>
#include <stdint.h>
#include <SDL3/SDL.h>

#define AI2_VIDEO_FRAME_WIDTH 256
#define AI2_VIDEO_FRAME_HEIGHT 240
#define AI2_VIDEO_FRAME_PIXELS (AI2_VIDEO_FRAME_WIDTH * AI2_VIDEO_FRAME_HEIGHT)

typedef struct AI2VideoOutput {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t pixels[AI2_VIDEO_FRAME_PIXELS];
    int frame_valid;
    int video_initialized;
    int vsync_enabled;
} AI2VideoOutput;

void ai2_video_output_initialize(AI2VideoOutput *output);
int ai2_video_output_open(AI2VideoOutput *output, HWND window, int vsync,
    wchar_t *error, size_t error_capacity);
void ai2_video_output_close(AI2VideoOutput *output);
int ai2_video_output_submit(AI2VideoOutput *output, const uint32_t *bgra_pixels);
int ai2_video_output_present(AI2VideoOutput *output, int integer_scale,
    int correct_aspect);

#endif
