#ifndef AI2_FRONTEND_INPUT_H
#define AI2_FRONTEND_INPUT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum Ai2FrontendButton {
    AI2_FRONTEND_BUTTON_A = 0,
    AI2_FRONTEND_BUTTON_B,
    AI2_FRONTEND_BUTTON_SELECT,
    AI2_FRONTEND_BUTTON_START,
    AI2_FRONTEND_BUTTON_UP,
    AI2_FRONTEND_BUTTON_DOWN,
    AI2_FRONTEND_BUTTON_LEFT,
    AI2_FRONTEND_BUTTON_RIGHT
} Ai2FrontendButton;

typedef struct Ai2FrontendInputState {
    uint8_t buttons[2];
} Ai2FrontendInputState;

void ai2_frontend_input_init(Ai2FrontendInputState *state);
void ai2_frontend_input_reset(Ai2FrontendInputState *state);
int ai2_frontend_input_submit(Ai2FrontendInputState *state, unsigned player,
                              Ai2FrontendButton button, int pressed);
uint8_t ai2_frontend_input_buttons(const Ai2FrontendInputState *state, unsigned player);
const char *ai2_frontend_button_name(Ai2FrontendButton button);
#ifdef __cplusplus
}
#endif
#endif
