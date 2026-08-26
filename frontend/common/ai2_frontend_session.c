#include "ai2_frontend_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void err(char *out,size_t n,const char *s){if(out&&n)snprintf(out,n,"%s",s?s:"");}
int ai2_frontend_session_open(Ai2FrontendSession *s,const char *path,char *error,size_t cap){FILE*f;long n;unsigned char*p;if(!s||!path){err(error,cap,"invalid frontend session argument");return 0;}memset(s,0,sizeof(*s));f=fopen(path,"rb");if(!f){err(error,cap,"ROM could not be opened");return 0;}if(fseek(f,0,SEEK_END)!=0||(n=ftell(f))<0||fseek(f,0,SEEK_SET)!=0){fclose(f);err(error,cap,"ROM size could not be read");return 0;}p=(unsigned char*)malloc((size_t)n);if(!p){fclose(f);err(error,cap,"ROM allocation failed");return 0;}if(fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);err(error,cap,"ROM read failed");return 0;}fclose(f);s->core=ai2_static_core_create();if(!s->core||!ai2_static_core_reset(s->core,p,(size_t)n)){if(s->core)ai2_static_core_destroy(s->core);s->core=NULL;free(p);err(error,cap,"ROM rejected by exact static-core profile");return 0;}free(p);err(error,cap,"");return 1;}
void ai2_frontend_session_close(Ai2FrontendSession*s){if(s&&s->core){ai2_static_core_destroy(s->core);s->core=NULL;}}
int ai2_frontend_session_advance(Ai2FrontendSession*s,uint8_t p1,uint8_t p2,Ai2FrameResult*r){return s&&s->core?ai2_static_core_advance_frame(s->core,p1,p2,0u,r):0;}
int ai2_frontend_session_copy_bgra(const Ai2FrontendSession*s,uint32_t*p,size_t n){return s&&s->core?ai2_static_core_frame_copy_bgra(s->core,p,n):0;}
size_t ai2_frontend_session_audio_read(Ai2FrontendSession*s,int16_t*p,size_t n){return s&&s->core?ai2_static_core_audio_read(s->core,p,n):0u;}
