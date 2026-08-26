#ifndef AI2_FRONTEND_SESSION_H
#define AI2_FRONTEND_SESSION_H
#include "include/ai2_static_core.h"
#include <stddef.h>
#include <stdint.h>
typedef struct Ai2FrontendSession { Ai2StaticCore *core; } Ai2FrontendSession;
int ai2_frontend_session_open(Ai2FrontendSession *session,const char *rom_path,char *error,size_t error_capacity);
void ai2_frontend_session_close(Ai2FrontendSession *session);
int ai2_frontend_session_advance(Ai2FrontendSession *session,uint8_t p1,uint8_t p2,Ai2FrameResult *result);
int ai2_frontend_session_copy_bgra(const Ai2FrontendSession *session,uint32_t *pixels,size_t capacity);
size_t ai2_frontend_session_audio_read(Ai2FrontendSession *session,int16_t *samples,size_t capacity);
#endif
