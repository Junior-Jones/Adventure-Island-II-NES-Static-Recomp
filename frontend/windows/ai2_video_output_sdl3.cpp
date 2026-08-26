#include "ai2_video_output_sdl3.h"

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <cstring>
#include <iterator>

namespace {
void set_error(wchar_t *error,size_t cap,const wchar_t *prefix) {
    wchar_t detail[256]={};
    if(!error||!cap)return;
    MultiByteToWideChar(CP_UTF8,0,SDL_GetError(),-1,detail,(int)std::size(detail));
    _snwprintf(error,cap,L"%s: %s",prefix,detail[0]?detail:L"Unknown SDL error");
    error[cap-1]=0;
}
}

void ai2_video_output_initialize(AI2VideoOutput *o){if(o)std::memset(o,0,sizeof(*o));}

int ai2_video_output_open(AI2VideoOutput *o,HWND hwnd,int vsync,wchar_t *error,size_t cap){
    if(!o||!hwnd)return 0;
    ai2_video_output_close(o);
    if(!SDL_InitSubSystem(SDL_INIT_VIDEO)){set_error(error,cap,L"Unable to initialize SDL video");return 0;}
    o->video_initialized=1;
    SDL_PropertiesID wp=SDL_CreateProperties();
    if(!wp||!SDL_SetPointerProperty(wp,SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER,hwnd)){
        if(wp)SDL_DestroyProperties(wp);set_error(error,cap,L"Unable to wrap the game window");ai2_video_output_close(o);return 0;}
    o->window=SDL_CreateWindowWithProperties(wp);SDL_DestroyProperties(wp);
    if(!o->window){set_error(error,cap,L"Unable to wrap the game window");ai2_video_output_close(o);return 0;}
    SDL_PropertiesID rp=SDL_CreateProperties();
    if(!rp||!SDL_SetPointerProperty(rp,SDL_PROP_RENDERER_CREATE_WINDOW_POINTER,o->window)||
       !SDL_SetStringProperty(rp,SDL_PROP_RENDERER_CREATE_NAME_STRING,"direct3d11,direct3d12,software")||
       !SDL_SetNumberProperty(rp,SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER,vsync?1:0)){
        if(rp)SDL_DestroyProperties(rp);set_error(error,cap,L"Unable to configure video output");ai2_video_output_close(o);return 0;}
    o->renderer=SDL_CreateRendererWithProperties(rp);SDL_DestroyProperties(rp);
    if(!o->renderer){set_error(error,cap,L"Unable to create video renderer");ai2_video_output_close(o);return 0;}
    o->texture=SDL_CreateTexture(o->renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,AI2_VIDEO_FRAME_WIDTH,AI2_VIDEO_FRAME_HEIGHT);
    if(!o->texture||!SDL_SetTextureScaleMode(o->texture,SDL_SCALEMODE_NEAREST)){
        set_error(error,cap,L"Unable to create game texture");ai2_video_output_close(o);return 0;}
    o->vsync_enabled=vsync!=0;return 1;
}

void ai2_video_output_close(AI2VideoOutput *o){
    if(!o)return;if(o->texture)SDL_DestroyTexture(o->texture);if(o->renderer)SDL_DestroyRenderer(o->renderer);
    if(o->window)SDL_DestroyWindow(o->window);if(o->video_initialized)SDL_QuitSubSystem(SDL_INIT_VIDEO);std::memset(o,0,sizeof(*o));
}

int ai2_video_output_submit(AI2VideoOutput *o,const uint32_t *bgra_pixels){
    if(!o||!bgra_pixels)return 0;
    std::memcpy(o->pixels,bgra_pixels,sizeof(o->pixels));
    o->frame_valid=1;return 1;
}

int ai2_video_output_present(AI2VideoOutput *o,int integer_scale,int correct_aspect){
    if(!o||!o->renderer||!o->texture||!o->frame_valid)return 0;
    int ow=0,oh=0;if(!SDL_GetRenderOutputSize(o->renderer,&ow,&oh))return 0;
    const int aspect_width=correct_aspect?4:AI2_VIDEO_FRAME_WIDTH;
    const int aspect_height=correct_aspect?3:AI2_VIDEO_FRAME_HEIGHT;
    int dw=0,dh=0;
    if((long long)ow*aspect_height<=(long long)oh*aspect_width){dw=ow;dh=ow*aspect_height/aspect_width;}
    else{dh=oh;dw=oh*aspect_width/aspect_height;}
    if(integer_scale>=1&&integer_scale<=4){
        const int requested_height=AI2_VIDEO_FRAME_HEIGHT*integer_scale;
        dh=std::min(oh,requested_height);dw=dh*aspect_width/aspect_height;
        if(dw>ow){dw=ow;dh=dw*aspect_height/aspect_width;}
    }
    SDL_FRect dst{(float)(ow-dw)/2.0f,(float)(oh-dh)/2.0f,(float)dw,(float)dh};
    if(!SDL_UpdateTexture(o->texture,nullptr,o->pixels,AI2_VIDEO_FRAME_WIDTH*(int)sizeof(uint32_t))||
       !SDL_SetRenderDrawColor(o->renderer,0,0,0,255)||!SDL_RenderClear(o->renderer)||
       !SDL_RenderTexture(o->renderer,o->texture,nullptr,&dst))return 0;
    SDL_RenderPresent(o->renderer);return 1;
}
