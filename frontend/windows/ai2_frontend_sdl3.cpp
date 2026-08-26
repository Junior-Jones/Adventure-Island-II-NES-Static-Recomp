#ifdef _WIN32
#define UNICODE
#define _UNICODE
#define SDL_MAIN_HANDLED

#include "ai2_game_entry.h"
#include "ai2_audio_output_sdl3.h"
#include "ai2_gamepad_input_win32.h"
#include "ai2_rom.h"
#include "ai2_windows_live.h"
#include "frontend/common/ai2_frontend_ipc.h"
#include "include/ai2_static_core.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace {
constexpr double kNesFps=60.0988138974405;
constexpr uint8_t kMasks[8]={0x10u,0x20u,0x40u,0x80u,0x02u,0x01u,0x08u,0x04u};

struct Options {
    std::wstring rom;
    std::wstring snapshot_dir;
    std::wstring screenshot_dir;
    HWND launcher{};
    int fullscreen{};
    int correct_aspect{};
    int vsync{};
    int pause_focus_loss{};
    int mute{};
    int scale{};
    int volume{70};
    int latency{60};
    int input_source{AI2_INPUT_COMBINED};
    int deadzone{35};
    UINT keys[8]{VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,'D','F','S','A'};
    int pad[8]{AI2_GAMEPAD_DPAD_UP,AI2_GAMEPAD_DPAD_DOWN,AI2_GAMEPAD_DPAD_LEFT,
               AI2_GAMEPAD_DPAD_RIGHT,AI2_GAMEPAD_FACE_WEST,AI2_GAMEPAD_FACE_SOUTH,
               AI2_GAMEPAD_START,AI2_GAMEPAD_BACK};
};

std::string utf8(const std::wstring &text)
{
    if(text.empty())return {};
    int count=WideCharToMultiByte(CP_UTF8,0,text.c_str(),-1,nullptr,0,nullptr,nullptr);
    if(count<=0)return {};
    std::string result((size_t)count,'\0');
    WideCharToMultiByte(CP_UTF8,0,text.c_str(),-1,result.data(),count,nullptr,nullptr);
    result.pop_back();
    return result;
}

bool number(const wchar_t *text,unsigned long long &value)
{
    if(!text||!*text||*text<L'0'||*text>L'9')return false;
    wchar_t *end=nullptr;
    errno=0;
    value=wcstoull(text,&end,10);
    return errno!=ERANGE&&end&&*end==0;
}

bool option_index(const std::wstring &option,const wchar_t *prefix,unsigned &index)
{
    size_t length=wcslen(prefix);
    if(option.compare(0,length,prefix)!=0||option.size()!=length+1)return false;
    wchar_t digit=option[length];
    if(digit<L'0'||digit>L'7')return false;
    index=(unsigned)(digit-L'0');return true;
}

bool parse_options(int argc,wchar_t **argv,Options &options)
{
    for(int index=2;index<argc;++index) {
        std::wstring arg=argv[index];
        auto next=[&]() -> const wchar_t * { return ++index<argc?argv[index]:nullptr; };
        if(arg==L"--rom") { const wchar_t *v=next();if(!v)return false;options.rom=v; }
        else if(arg==L"--snapshot-dir") { const wchar_t *v=next();if(!v)return false;options.snapshot_dir=v; }
        else if(arg==L"--screenshot-dir") { const wchar_t *v=next();if(!v)return false;options.screenshot_dir=v; }
        else if(arg==L"--fullscreen") options.fullscreen=1;
        else if(arg==L"--correct-aspect") options.correct_aspect=1;
        else if(arg==L"--vsync") options.vsync=1;
        else if(arg==L"--pause-on-focus-loss") options.pause_focus_loss=1;
        else if(arg==L"--mute") options.mute=1;
        else {
            const wchar_t *v=next();unsigned long long parsed=0;
            if(!v||!number(v,parsed))return false;
            if(arg==L"--launcher-hwnd"){if(parsed>(unsigned long long)UINTPTR_MAX)return false;options.launcher=(HWND)(uintptr_t)parsed;}
            else if(arg==L"--scale"){if(parsed>4u)return false;options.scale=(int)parsed;}
            else if(arg==L"--volume"){if(parsed>100u)return false;options.volume=(int)parsed;}
            else if(arg==L"--audio-latency"){if(parsed<20u||parsed>2000u)return false;options.latency=(int)parsed;}
            else if(arg==L"--input-source"){if(parsed>(unsigned long long)AI2_INPUT_COMBINED)return false;options.input_source=(int)parsed;}
            else if(arg==L"--deadzone"){if(parsed<10u||parsed>90u)return false;options.deadzone=(int)parsed;}
            else if(arg.rfind(L"--key-",0)==0) {
                unsigned key_index=0;if(!option_index(arg,L"--key-",key_index)||parsed==0u||parsed>=255u)return false;
                options.keys[key_index]=(UINT)parsed;
            } else if(arg.rfind(L"--pad-",0)==0) {
                unsigned pad_index=0;if(!option_index(arg,L"--pad-",pad_index)||parsed>(unsigned long long)AI2_GAMEPAD_CONTROL_LAST)return false;
                options.pad[pad_index]=(int)parsed;
            } else return false;
        }
    }
    return !options.rom.empty()&&!options.snapshot_dir.empty()&&!options.screenshot_dir.empty();
}

std::wstring object_name(const wchar_t *kind)
{
    return L"Local\\AdventureIslandII"+std::wstring(kind)+L"-"+std::to_wstring(GetCurrentProcessId());
}

HANDLE make_event(const wchar_t *kind)
{
    std::wstring name=object_name(kind);
    return CreateEventW(nullptr,FALSE,FALSE,name.c_str());
}

uint8_t sanitize(uint8_t input)
{
    if((input&0x30u)==0x30u)input&=0xcfu;
    if((input&0xc0u)==0xc0u)input&=0x3fu;
    return input;
}

uint8_t keyboard_buttons(const Options &options)
{
    uint8_t result=0;
    if(options.input_source==AI2_INPUT_GAMEPAD)return 0;
    for(unsigned index=0;index<8u;++index)
        if(GetAsyncKeyState((int)options.keys[index])&0x8000)result|=kMasks[index];
    return sanitize(result);
}

UINT launcher_shortcut(SDL_Keycode key)
{
    switch(key){case SDLK_F1:return VK_F1;case SDLK_F2:return VK_F2;case SDLK_F3:return VK_F3;case SDLK_F4:return VK_F4;case SDLK_F5:return VK_F5;case SDLK_F6:return VK_F6;case SDLK_F7:return VK_F7;case SDLK_F8:return VK_F8;default:return 0;}
}

bool write_all(HANDLE file,const void *data,DWORD size)
{
    const unsigned char *bytes=static_cast<const unsigned char *>(data);
    while(size){DWORD written=0;if(!WriteFile(file,bytes,size,&written,nullptr)||!written)return false;bytes+=written;size-=written;}
    return true;
}

bool save_bmp(Ai2StaticCore *core,const std::wstring &directory)
{
    if(!CreateDirectoryW(directory.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    SYSTEMTIME time{};GetLocalTime(&time);
    std::wstring path;HANDLE file=INVALID_HANDLE_VALUE;
    for(unsigned suffix=0;suffix<10000u;++suffix){
        wchar_t name[192]{};
        _snwprintf(name,std::size(name),L"Adventure Island II %04u-%02u-%02u %02u-%02u-%02u-%03u-%04u.bmp",
            time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,suffix);
        path=directory+L"\\"+name;
        file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file!=INVALID_HANDLE_VALUE)break;
        if(GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    }
    if(file==INVALID_HANDLE_VALUE)return false;
    unsigned char header[54]{};
    auto put16=[](unsigned char *p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);};
    auto put32=[](unsigned char *p,unsigned long v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);};
    header[0]='B';header[1]='M';put32(header+2,54u+256u*240u*3u);put32(header+10,54);put32(header+14,40);
    put32(header+18,256);put32(header+22,240);put16(header+26,1);put16(header+28,24);put32(header+34,256u*240u*3u);
    bool ok=write_all(file,header,(DWORD)sizeof(header));
    std::array<uint32_t,AI2_CORE_FRAME_PIXELS> frame{};
    if(ok)ok=ai2_static_core_frame_copy_bgra(core,frame.data(),frame.size())!=0;
    for(int y=239;ok&&y>=0;--y)for(unsigned x=0;x<256u;++x){uint32_t c=frame[(unsigned)y*256u+x];unsigned char bgr[3]={(unsigned char)c,(unsigned char)(c>>8),(unsigned char)(c>>16)};if(!write_all(file,bgr,3u)){ok=false;break;}}
    if(!CloseHandle(file))ok=false;
    if(!ok)DeleteFileW(path.c_str());
    return ok;
}

void return_to_launcher(SDL_Window *window,const Options &options,AI2AudioOutput &audio,bool &paused)
{
    paused=true;ai2_audio_output_pause(&audio);SDL_HideWindow(window);
    if(options.launcher&&IsWindow(options.launcher)) {
        DWORD launcher_pid=0;GetWindowThreadProcessId(options.launcher,&launcher_pid);
        if(launcher_pid)AllowSetForegroundWindow(launcher_pid);
        ShowWindow(options.launcher,SW_RESTORE);BringWindowToTop(options.launcher);SetForegroundWindow(options.launcher);
        PostMessageW(options.launcher,AI2_WM_GAME_RETURN,0,0);
    }
}

bool present(SDL_Renderer *renderer,SDL_Texture *texture,const uint32_t *pixels,const Options &options)
{
    int width=0,height=0;if(!SDL_GetRenderOutputSize(renderer,&width,&height))return false;
    int aspect_width=options.correct_aspect?4:256,aspect_height=options.correct_aspect?3:240;
    int draw_width=0,draw_height=0;
    if((long long)width*aspect_height<=(long long)height*aspect_width){draw_width=width;draw_height=width*aspect_height/aspect_width;}
    else{draw_height=height;draw_width=height*aspect_width/aspect_height;}
    if(options.scale>=1&&options.scale<=4){draw_height=std::min(height,240*options.scale);draw_width=draw_height*aspect_width/aspect_height;if(draw_width>width){draw_width=width;draw_height=draw_width*aspect_height/aspect_width;}}
    SDL_FRect target{(float)(width-draw_width)/2.0f,(float)(height-draw_height)/2.0f,(float)draw_width,(float)draw_height};
    return SDL_UpdateTexture(texture,nullptr,pixels,256*(int)sizeof(uint32_t))&&
           SDL_SetRenderDrawColor(renderer,0,0,0,255)&&SDL_RenderClear(renderer)&&
           SDL_RenderTexture(renderer,texture,nullptr,&target)&&SDL_RenderPresent(renderer);
}
}

int ai2_game_main(int argc,wchar_t **argv)
{
    Options options{};if(!parse_options(argc,argv,options)){std::fprintf(stderr,"Invalid game-mode command line.\n");return 2;}
    std::string rom_path=utf8(options.rom);AI2Rom rom{};char error[256]{};
    if(!ai2_rom_load(rom_path.c_str(),&rom,error,sizeof(error))||!ai2_rom_is_expected(&rom,error,sizeof(error))){std::fprintf(stderr,"ROM validation failed: %s\n",error);ai2_rom_free(&rom);return 2;}
    Ai2StaticCore *core=ai2_static_core_create();
    if(!core||!ai2_static_core_reset(core,rom.file_data,rom.file_size)){std::fprintf(stderr,"Static core reset failed.\n");ai2_static_core_destroy(core);ai2_rom_free(&rom);return 2;}
    ai2_rom_free(&rom);

    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS|SDL_INIT_GAMEPAD)){std::fprintf(stderr,"SDL initialization failed: %s\n",SDL_GetError());ai2_static_core_destroy(core);return 2;}
    int initial_scale=options.scale>=1&&options.scale<=4?options.scale:3;
    int initial_width=(options.correct_aspect?320:256)*initial_scale,initial_height=240*initial_scale;
    SDL_Window *window=SDL_CreateWindow("Adventure Island II (NES)",initial_width,initial_height,SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_Renderer *renderer=window?SDL_CreateRenderer(window,nullptr):nullptr;
    SDL_Texture *texture=renderer?SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,256,240):nullptr;
    if(!window||!renderer||!texture){std::fprintf(stderr,"Game window creation failed: %s\n",SDL_GetError());if(texture)SDL_DestroyTexture(texture);if(renderer)SDL_DestroyRenderer(renderer);if(window)SDL_DestroyWindow(window);SDL_Quit();ai2_static_core_destroy(core);return 2;}
    SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_NEAREST);if(options.vsync)SDL_SetRenderVSync(renderer,1);if(options.fullscreen)SDL_SetWindowFullscreen(window,true);

    HANDLE resume=make_event(L"Resume"),pause=make_event(L"Pause"),save=make_event(L"Save"),load=make_event(L"Load"),capture=make_event(L"Capture"),audio_event=make_event(L"Audio");
    std::wstring mapping_name=object_name(L"AudioConfig");
    HANDLE audio_mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Ai2FrontendAudioConfig),mapping_name.c_str());
    auto *audio_config=audio_mapping?static_cast<Ai2FrontendAudioConfig *>(MapViewOfFile(audio_mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Ai2FrontendAudioConfig))):nullptr;
    std::wstring command_mapping_name=object_name(L"CommandState");
    HANDLE command_mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Ai2FrontendCommandState),command_mapping_name.c_str());
    auto *command_state=command_mapping?static_cast<Ai2FrontendCommandState *>(MapViewOfFile(command_mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Ai2FrontendCommandState))):nullptr;
    if(!resume||!pause||!save||!load||!capture||!audio_event||!audio_mapping||!audio_config||!command_mapping||!command_state){std::fprintf(stderr,"Launcher IPC setup failed.\n");return 2;}
    std::memset(command_state,0,sizeof(*command_state));
    audio_config->enabled=options.mute?0:1;audio_config->volume=options.volume;audio_config->latency_ms=options.latency;

    AI2AudioOutput audio{};ai2_audio_output_initialize(&audio);wchar_t audio_error[384]{};
    if(!options.mute&&!ai2_audio_output_open(&audio,options.volume,options.latency,audio_error,std::size(audio_error)))std::fprintf(stderr,"Audio unavailable.\n");
    if(!options.mute)ai2_audio_output_resume(&audio);
    AI2GamepadInputWin32 gamepad{};std::wstring database;
    {wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,(DWORD)std::size(module));database=module;size_t slash=database.find_last_of(L"\\/");database=(slash==std::wstring::npos?L".":database.substr(0,slash))+L"\\gamecontrollerdb.txt";}
    (void)ai2_gamepad_win32_initialize(&gamepad,database.c_str(),0u,nullptr);

    CreateDirectoryW(options.snapshot_dir.c_str(),nullptr);CreateDirectoryW(options.screenshot_dir.c_str(),nullptr);
    std::wstring snapshot_w=options.snapshot_dir+L"\\Quick Save.ai2state";std::string snapshot=utf8(snapshot_w);
    std::array<uint32_t,AI2_CORE_FRAME_PIXELS> frame{};bool running=true,paused=false;int rc=0;
    auto next_frame=std::chrono::steady_clock::now();const auto frame_duration=std::chrono::duration<double>(1.0/kNesFps);
    while(running) {
        SDL_Event event{};while(SDL_PollEvent(&event)){
            if(event.type==SDL_EVENT_QUIT)running=false;
            else if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&event.key.key==SDLK_ESCAPE)return_to_launcher(window,options,audio,paused);
            else if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&launcher_shortcut(event.key.key)&&options.launcher&&IsWindow(options.launcher))PostMessageW(options.launcher,WM_KEYDOWN,launcher_shortcut(event.key.key),0);
            else if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&event.key.key==SDLK_F11){options.fullscreen=!options.fullscreen;SDL_SetWindowFullscreen(window,options.fullscreen!=0);}
            else if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST&&options.pause_focus_loss&&!paused){paused=true;ai2_audio_output_pause(&audio);}
            else if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED&&paused&&SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS){paused=false;if(!options.mute)ai2_audio_output_resume(&audio);}
        }
        if(pause&&WaitForSingleObject(pause,0)==WAIT_OBJECT_0&&!paused)return_to_launcher(window,options,audio,paused);
        if(resume&&WaitForSingleObject(resume,0)==WAIT_OBJECT_0){paused=false;SDL_ShowWindow(window);SDL_RaiseWindow(window);if(!options.mute)ai2_audio_output_resume(&audio);next_frame=std::chrono::steady_clock::now();}
        if(save&&WaitForSingleObject(save,0)==WAIT_OBJECT_0){char snapshot_error[256]{};bool ok=ai2_static_core_snapshot_save(core,snapshot.c_str(),snapshot_error,sizeof(snapshot_error))==AI2_SNAPSHOT_OK;std::fprintf(stdout,"SNAPSHOT_SAVE %s%s%s\n",ok?"OK":"FAILED",ok?"":": ",ok?"":snapshot_error);std::fflush(stdout);InterlockedExchange(&command_state->save_result,ok?AI2_IPC_SUCCEEDED:AI2_IPC_FAILED);}
        if(load&&WaitForSingleObject(load,0)==WAIT_OBJECT_0){char snapshot_error[256]{};bool ok=ai2_static_core_snapshot_load(core,snapshot.c_str(),snapshot_error,sizeof(snapshot_error))==AI2_SNAPSHOT_OK;if(ok)ai2_audio_output_flush(&audio);std::fprintf(stdout,"SNAPSHOT_LOAD %s%s%s\n",ok?"OK":"FAILED",ok?"":": ",ok?"":snapshot_error);std::fflush(stdout);InterlockedExchange(&command_state->load_result,ok?AI2_IPC_SUCCEEDED:AI2_IPC_FAILED);}
        if(capture&&WaitForSingleObject(capture,0)==WAIT_OBJECT_0){bool ok=save_bmp(core,options.screenshot_dir);std::fprintf(stdout,"SCREENSHOT %s\n",ok?"OK":"FAILED");std::fflush(stdout);InterlockedExchange(&command_state->capture_result,ok?AI2_IPC_SUCCEEDED:AI2_IPC_FAILED);}
        if(audio_event&&WaitForSingleObject(audio_event,0)==WAIT_OBJECT_0&&audio_config){options.mute=!audio_config->enabled;options.volume=(int)audio_config->volume;options.latency=(int)audio_config->latency_ms;ai2_audio_output_close(&audio);ai2_audio_output_initialize(&audio);bool ok=options.mute||ai2_audio_output_open(&audio,options.volume,options.latency,audio_error,std::size(audio_error));if(ok&&!options.mute)ai2_audio_output_resume(&audio);InterlockedExchange(&command_state->audio_result,ok?AI2_IPC_SUCCEEDED:AI2_IPC_FAILED);}
        if(paused){std::this_thread::sleep_for(std::chrono::milliseconds(10));continue;}

        ai2_gamepad_win32_begin_frame(&gamepad,1u);
        uint8_t pad_buttons=options.input_source==AI2_INPUT_KEYBOARD?0u:(uint8_t)ai2_gamepad_win32_poll(&gamepad,options.pad,options.deadzone);
        uint8_t buttons=sanitize((uint8_t)(keyboard_buttons(options)|pad_buttons));Ai2FrameResult result{};
        if(!ai2_static_core_advance_frame(core,buttons,0u,0u,&result)||!result.completed||result.stopped){std::fprintf(stderr,"Static core stopped at frame %llu, PC $%04X, status %s.\n",(unsigned long long)result.end_frame,result.program_counter,ai2_static_core_status_name(ai2_static_core_status(core)));rc=2;break;}
        if(!ai2_static_core_frame_copy_bgra(core,frame.data(),frame.size())||!present(renderer,texture,frame.data(),options)){std::fprintf(stderr,"Frame presentation failed: %s\n",SDL_GetError());rc=2;break;}
        if(!options.mute){int16_t pcm[2048];size_t count=0;while((count=ai2_static_core_audio_read(core,pcm,std::size(pcm)))>0u)ai2_audio_output_push(&audio,pcm,count);}else ai2_static_core_audio_clear(core);
        next_frame+=std::chrono::duration_cast<std::chrono::steady_clock::duration>(frame_duration);std::this_thread::sleep_until(next_frame);if(next_frame<std::chrono::steady_clock::now()-std::chrono::milliseconds(250))next_frame=std::chrono::steady_clock::now();
    }

    ai2_gamepad_win32_shutdown(&gamepad);ai2_audio_output_close(&audio);
    if(command_state)UnmapViewOfFile(command_state);if(command_mapping)CloseHandle(command_mapping);
    if(audio_config)UnmapViewOfFile(audio_config);if(audio_mapping)CloseHandle(audio_mapping);
    if(resume)CloseHandle(resume);if(pause)CloseHandle(pause);if(save)CloseHandle(save);if(load)CloseHandle(load);if(capture)CloseHandle(capture);if(audio_event)CloseHandle(audio_event);
    SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();ai2_static_core_destroy(core);return rc;
}
#endif
