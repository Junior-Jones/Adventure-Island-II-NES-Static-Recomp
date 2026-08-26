#ifndef AI2_WINDOWS_LIVE_H
#define AI2_WINDOWS_LIVE_H

#ifdef _WIN32
#include <windows.h>
#include <stddef.h>
#include "frontend/common/ai2_frontend_ipc.h"

#define AI2_NES_PLAYER_COUNT 2
#define AI2_NES_BINDING_COUNT 8
#define AI2_INPUT_KEYBOARD 0
#define AI2_INPUT_GAMEPAD 1
#define AI2_INPUT_COMBINED 2

enum AI2NesBindingAction {
    AI2_BIND_UP=0, AI2_BIND_DOWN, AI2_BIND_LEFT, AI2_BIND_RIGHT,
    AI2_BIND_B, AI2_BIND_A, AI2_BIND_START, AI2_BIND_SELECT
};

struct AI2LiveSettings {
    int fullscreen;
    int integer_scale;
    int correct_aspect;
    int vsync;
    int pause_on_focus_loss;
    int audio_enabled;
    int volume_percent;
    int audio_latency_ms;
    int input_source;
    int gamepad_deadzone_percent;
    UINT bindings[AI2_NES_PLAYER_COUNT][AI2_NES_BINDING_COUNT];
    int gamepad_bindings[AI2_NES_BINDING_COUNT];
};

void ai2_windows_live_settings_defaults(AI2LiveSettings *settings);
bool ai2_windows_live_set_next_settings(const AI2LiveSettings *settings);
bool ai2_windows_live_start(HWND owner, const wchar_t *rom_path,
    wchar_t *error_text, size_t error_text_count);
void ai2_windows_live_stop(void);
bool ai2_windows_live_toggle_pause(void);
void ai2_windows_live_resize(int x, int y, int width, int height);
void ai2_windows_live_key_event(UINT message, WPARAM key);
HANDLE ai2_windows_live_frame_timer(void);
void ai2_windows_live_service_frame_timer(void);
bool ai2_windows_live_quick_save(void);
bool ai2_windows_live_quick_load(void);
bool ai2_windows_live_take_screenshot(void);
bool ai2_windows_live_is_running(void);
bool ai2_windows_live_is_paused(void);
void ai2_windows_live_mark_paused(void);
#endif

#endif
