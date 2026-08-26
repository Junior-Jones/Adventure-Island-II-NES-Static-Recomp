#define _POSIX_C_SOURCE 200809L
#include "frontend/common/ai2_frontend_input.h"
#include "frontend/common/ai2_frontend_session.h"
#include "frontend/linux/ai2_linux_audio.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SCALE 3
#define NES_FPS 60.0988138974405

typedef struct KeyMap {
    KeySym key;
    Ai2FrontendButton button;
} KeyMap;

static const KeyMap key_map[] = {
    {XK_Up, AI2_FRONTEND_BUTTON_UP},
    {XK_Down, AI2_FRONTEND_BUTTON_DOWN},
    {XK_Left, AI2_FRONTEND_BUTTON_LEFT},
    {XK_Right, AI2_FRONTEND_BUTTON_RIGHT},
    {XK_z, AI2_FRONTEND_BUTTON_A}, {XK_Z, AI2_FRONTEND_BUTTON_A},
    {XK_x, AI2_FRONTEND_BUTTON_B}, {XK_X, AI2_FRONTEND_BUTTON_B},
    {XK_Return, AI2_FRONTEND_BUTTON_START},
    {XK_Shift_R, AI2_FRONTEND_BUTTON_SELECT},
    {XK_Shift_L, AI2_FRONTEND_BUTTON_SELECT}
};

static double now_seconds(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (double)value.tv_sec + (double)value.tv_nsec / 1e9;
}

static void sleep_until(double target)
{
    for(;;) {
        double delta = target - now_seconds();
        struct timespec delay;
        if(delta <= 0.0) return;
        delay.tv_sec = (time_t)delta;
        delay.tv_nsec = (long)((delta - (double)delay.tv_sec) * 1e9);
        nanosleep(&delay, NULL);
    }
}

static int map_key(KeySym key, Ai2FrontendButton *button)
{
    size_t i;
    for(i = 0; i < sizeof(key_map) / sizeof(key_map[0]); i++) {
        if(key_map[i].key == key) {
            *button = key_map[i].button;
            return 1;
        }
    }
    return 0;
}

static int parse_frame_limit(int argc, char **argv, uint64_t *frame_limit)
{
    char *end = NULL;
    unsigned long long parsed;
    *frame_limit = 0u;
    if(argc == 2) return 1;
    if(argc != 4 || strcmp(argv[2], "--frames") != 0) return 0;
    errno = 0;
    parsed = strtoull(argv[3], &end, 10);
    if(errno != 0 || end == NULL || *end != '\0' || parsed == 0u) return 0;
    *frame_limit = (uint64_t)parsed;
    return 1;
}

int main(int argc, char **argv)
{
    Display *display = NULL;
    Window window = 0;
    GC gc = 0;
    XEvent event;
    XImage *image = NULL;
    Visual *visual;
    int screen, depth;
    int running = 1, paused = 0;
    Ai2FrontendSession session;
    Ai2FrontendInputState input;
    Ai2LinuxAudio audio;
    Ai2FrameResult frame_result;
    char error[256] = {0};
    uint32_t *frame = NULL, *scaled = NULL;
    double next_frame;
    size_t audio_count;
    int16_t pcm[4096];
    uint64_t frame_limit = 0u;
    int rc = 0;

    if(!parse_frame_limit(argc, argv, &frame_limit)) {
        fprintf(stderr, "usage: %s ROM.nes [--frames N]\n", argv[0]);
        return 2;
    }
    if(!ai2_frontend_session_open(&session, argv[1], error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 3;
    }
    ai2_frontend_input_init(&input);
    memset(&audio, 0, sizeof(audio));
    (void)ai2_linux_audio_open(&audio, 44100u);

    display = XOpenDisplay(NULL);
    if(display == NULL) {
        fprintf(stderr, "X11 display unavailable\n");
        rc = 4;
        goto done;
    }
    screen = DefaultScreen(display);
    visual = DefaultVisual(display, screen);
    depth = DefaultDepth(display, screen);
    window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0,
        AI2_CORE_FRAME_WIDTH * SCALE, AI2_CORE_FRAME_HEIGHT * SCALE, 0,
        BlackPixel(display, screen), BlackPixel(display, screen));
    XStoreName(display, window, "Adventure Island II - Static Recomp");
    XSelectInput(display, window, ExposureMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask);
    XMapWindow(display, window);
    gc = XCreateGC(display, window, 0, NULL);

    frame = (uint32_t *)malloc((size_t)AI2_CORE_FRAME_PIXELS * sizeof(*frame));
    scaled = (uint32_t *)malloc((size_t)AI2_CORE_FRAME_WIDTH * SCALE * AI2_CORE_FRAME_HEIGHT * SCALE * sizeof(*scaled));
    if(frame == NULL || scaled == NULL) {
        fprintf(stderr, "frontend frame allocation failed\n");
        rc = 5;
        goto done;
    }
    image = XCreateImage(display, visual, (unsigned)depth, ZPixmap, 0, (char *)scaled,
        AI2_CORE_FRAME_WIDTH * SCALE, AI2_CORE_FRAME_HEIGHT * SCALE, 32, 0);
    if(image == NULL) {
        fprintf(stderr, "X11 image creation failed\n");
        rc = 5;
        goto done;
    }

    next_frame = now_seconds();
    while(running) {
        while(XPending(display)) {
            KeySym key;
            Ai2FrontendButton button;
            XNextEvent(display, &event);
            if(event.type == KeyPress || event.type == KeyRelease) {
                key = XLookupKeysym(&event.xkey, 0);
                if(event.type == KeyPress && key == XK_Escape) {
                    running = 0;
                    continue;
                }
                if(event.type == KeyPress && (key == XK_p || key == XK_P)) {
                    paused = !paused;
                    continue;
                }
                if(map_key(key, &button)) {
                    ai2_frontend_input_submit(&input, 0u, button, event.type == KeyPress);
                }
            }
        }
        if(!running) break;

        if(!paused) {
            if(!ai2_frontend_session_advance(&session, ai2_frontend_input_buttons(&input, 0u), 0u, &frame_result) || !frame_result.completed) {
                fprintf(stderr, "static core stopped at frame=%" PRIu64 " pc=%04X status=%s\n",
                    frame_result.end_frame, frame_result.program_counter,
                    ai2_static_core_status_name(ai2_static_core_status(session.core)));
                rc = 6;
                break;
            }
            if(ai2_frontend_session_copy_bgra(&session, frame, AI2_CORE_FRAME_PIXELS)) {
                unsigned y, x, dy, dx;
                for(y = 0; y < AI2_CORE_FRAME_HEIGHT; y++) {
                    for(x = 0; x < AI2_CORE_FRAME_WIDTH; x++) {
                        uint32_t color = frame[(size_t)y * AI2_CORE_FRAME_WIDTH + x];
                        for(dy = 0; dy < SCALE; dy++) {
                            for(dx = 0; dx < SCALE; dx++) {
                                scaled[(size_t)(y * SCALE + dy) * (AI2_CORE_FRAME_WIDTH * SCALE) + (x * SCALE + dx)] = color;
                            }
                        }
                    }
                }
                XPutImage(display, window, gc, image, 0, 0, 0, 0,
                    AI2_CORE_FRAME_WIDTH * SCALE, AI2_CORE_FRAME_HEIGHT * SCALE);
                XFlush(display);
            }
            while((audio_count = ai2_frontend_session_audio_read(&session, pcm, sizeof(pcm) / sizeof(pcm[0]))) > 0u) {
                ai2_linux_audio_write(&audio, pcm, audio_count);
            }
            if(frame_limit != 0u && frame_result.end_frame >= frame_limit) {
                printf("HEADED_STATUS mode=x11 frames=%" PRIu64 " pc=%04X status=ok\n",
                    frame_result.end_frame, frame_result.program_counter);
                fflush(stdout);
                running = 0;
            }
        }
        next_frame += 1.0 / NES_FPS;
        if(next_frame < now_seconds() - 0.25) next_frame = now_seconds();
        sleep_until(next_frame);
    }

done:
    if(image != NULL) {
        image->data = NULL;
        XDestroyImage(image);
    }
    free(scaled);
    free(frame);
    if(display != NULL) {
        if(gc != 0) XFreeGC(display, gc);
        if(window != 0) XDestroyWindow(display, window);
        XCloseDisplay(display);
    }
    ai2_linux_audio_close(&audio);
    ai2_frontend_session_close(&session);
    return rc;
}
