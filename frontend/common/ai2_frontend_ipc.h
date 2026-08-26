#ifndef AI2_FRONTEND_IPC_H
#define AI2_FRONTEND_IPC_H

#ifdef _WIN32
#include <windows.h>

#define AI2_WM_GAME_RETURN (WM_APP + 0x314)
#define AI2_WM_GAME_EXITED (WM_APP + 0x315)

typedef struct Ai2FrontendAudioConfig {
    volatile LONG enabled;
    volatile LONG volume;
    volatile LONG latency_ms;
} Ai2FrontendAudioConfig;

#define AI2_IPC_PENDING 0L
#define AI2_IPC_SUCCEEDED 1L
#define AI2_IPC_FAILED (-1L)

typedef struct Ai2FrontendCommandState {
    volatile LONG save_result;
    volatile LONG load_result;
    volatile LONG capture_result;
    volatile LONG audio_result;
} Ai2FrontendCommandState;
#endif

#endif
