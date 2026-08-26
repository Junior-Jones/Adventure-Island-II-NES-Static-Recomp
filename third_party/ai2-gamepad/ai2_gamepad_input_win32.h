#ifndef AI2_GAMEPAD_INPUT_WIN32_H
#define AI2_GAMEPAD_INPUT_WIN32_H

#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#include <SDL3/SDL_gamepad.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AI2_GAMEPAD_BINDING_COUNT 8

typedef enum AI2GamepadControl {
    AI2_GAMEPAD_DPAD_UP = 1,
    AI2_GAMEPAD_DPAD_DOWN,
    AI2_GAMEPAD_DPAD_LEFT,
    AI2_GAMEPAD_DPAD_RIGHT,
    AI2_GAMEPAD_LEFT_STICK_UP,
    AI2_GAMEPAD_LEFT_STICK_DOWN,
    AI2_GAMEPAD_LEFT_STICK_LEFT,
    AI2_GAMEPAD_LEFT_STICK_RIGHT,
    AI2_GAMEPAD_FACE_SOUTH,
    AI2_GAMEPAD_FACE_EAST,
    AI2_GAMEPAD_FACE_WEST,
    AI2_GAMEPAD_FACE_NORTH,
    AI2_GAMEPAD_LEFT_SHOULDER,
    AI2_GAMEPAD_RIGHT_SHOULDER,
    AI2_GAMEPAD_LEFT_TRIGGER,
    AI2_GAMEPAD_RIGHT_TRIGGER,
    AI2_GAMEPAD_START,
    AI2_GAMEPAD_BACK,
    AI2_GAMEPAD_LEFT_STICK_BUTTON,
    AI2_GAMEPAD_RIGHT_STICK_BUTTON,
    AI2_GAMEPAD_CONTROL_LAST = AI2_GAMEPAD_RIGHT_STICK_BUTTON
} AI2GamepadControl;

typedef struct AI2GamepadInputWin32 {
    SDL_Gamepad *handle;
    int initialized;
    int startup_gamepad_found;
    unsigned player_index;
    unsigned refresh_countdown;
    uint32_t analog_latched;
    char preferred_guid[64];
    char guid[64];
    wchar_t name[160];
} AI2GamepadInputWin32;

int ai2_gamepad_win32_initialize(AI2GamepadInputWin32 *input,
                                     const wchar_t *mapping_path,
                                     unsigned player_index,
                                     const wchar_t *preferred_guid);
void ai2_gamepad_win32_shutdown(AI2GamepadInputWin32 *input);
void ai2_gamepad_win32_begin_frame(AI2GamepadInputWin32 *inputs,
                                       size_t input_count);
uint16_t ai2_gamepad_win32_poll(
    AI2GamepadInputWin32 *input,
    const int bindings[AI2_GAMEPAD_BINDING_COUNT],
    int deadzone_percent);
int ai2_gamepad_win32_connected(const AI2GamepadInputWin32 *input);
const wchar_t *ai2_gamepad_win32_name(const AI2GamepadInputWin32 *input);
void ai2_gamepad_win32_guid(const AI2GamepadInputWin32 *input,
                                wchar_t *guid, size_t capacity);
void ai2_gamepad_win32_default_bindings(
    int bindings[AI2_GAMEPAD_BINDING_COUNT]);
const wchar_t *ai2_gamepad_win32_control_name(int control);
int ai2_gamepad_win32_capture_control(AI2GamepadInputWin32 *input);
void ai2_gamepad_win32_control_display_name(
    const AI2GamepadInputWin32 *input, int control,
    wchar_t *text, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
