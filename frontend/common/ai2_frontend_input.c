#include "ai2_frontend_input.h"
#include <string.h>

static uint8_t button_mask(Ai2FrontendButton b)
{
    switch(b) {
    case AI2_FRONTEND_BUTTON_A: return 0x01u;
    case AI2_FRONTEND_BUTTON_B: return 0x02u;
    case AI2_FRONTEND_BUTTON_SELECT: return 0x04u;
    case AI2_FRONTEND_BUTTON_START: return 0x08u;
    case AI2_FRONTEND_BUTTON_UP: return 0x10u;
    case AI2_FRONTEND_BUTTON_DOWN: return 0x20u;
    case AI2_FRONTEND_BUTTON_LEFT: return 0x40u;
    case AI2_FRONTEND_BUTTON_RIGHT: return 0x80u;
    default: return 0u;
    }
}

static uint8_t sanitize(uint8_t v)
{
    if((v & 0x30u) == 0x30u) v &= (uint8_t)~0x30u;
    if((v & 0xC0u) == 0xC0u) v &= (uint8_t)~0xC0u;
    return v;
}

void ai2_frontend_input_init(Ai2FrontendInputState *s) { if(s) memset(s,0,sizeof(*s)); }
void ai2_frontend_input_reset(Ai2FrontendInputState *s) { ai2_frontend_input_init(s); }
int ai2_frontend_input_submit(Ai2FrontendInputState *s,unsigned p,Ai2FrontendButton b,int pressed)
{
    uint8_t m;
    if(!s || p >= 2u || (m=button_mask(b)) == 0u) return 0;
    if(pressed) s->buttons[p] |= m; else s->buttons[p] &= (uint8_t)~m;
    s->buttons[p] = sanitize(s->buttons[p]);
    return 1;
}
uint8_t ai2_frontend_input_buttons(const Ai2FrontendInputState *s,unsigned p){return s&&p<2u?s->buttons[p]:0u;}
const char *ai2_frontend_button_name(Ai2FrontendButton b)
{
    switch(b){case AI2_FRONTEND_BUTTON_A:return "A";case AI2_FRONTEND_BUTTON_B:return "B";
    case AI2_FRONTEND_BUTTON_SELECT:return "Select";case AI2_FRONTEND_BUTTON_START:return "Start";
    case AI2_FRONTEND_BUTTON_UP:return "Up";case AI2_FRONTEND_BUTTON_DOWN:return "Down";
    case AI2_FRONTEND_BUTTON_LEFT:return "Left";case AI2_FRONTEND_BUTTON_RIGHT:return "Right";default:return "Unknown";}
}
