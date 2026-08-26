#ifdef _WIN32
#define UNICODE
#define _UNICODE

#include "ai2_windows_live.h"
#include "ai2_gamepad_input_win32.h"

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr const wchar_t *kGameTitle = L"Adventure Island II (NES)";

struct ProcessState {
    HANDLE process{};
    DWORD process_id{};
    HWND window{};
    HWND owner{};
    bool paused{};
};

AI2LiveSettings g_next{};
bool g_next_ready{};
ProcessState g_game{};

std::wstring exe_path()
{
    std::array<wchar_t,32768> path{};
    DWORD count = GetModuleFileNameW(nullptr,path.data(),(DWORD)path.size());
    return count && count < path.size() ? std::wstring(path.data(),count) : std::wstring();
}

std::wstring exe_directory()
{
    std::wstring path = exe_path();
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0,slash);
}

void append_quoted(std::wstring &command,const wchar_t *value)
{
    command += L" \"";
    for(const wchar_t *p=value; p && *p; ++p) {
        if(*p == L'\"') command += L'\\';
        command += *p;
    }
    command += L'\"';
}

void append_number(std::wstring &command,const wchar_t *name,unsigned long long value)
{
    command += L' ';
    command += name;
    command += L' ';
    command += std::to_wstring(value);
}

BOOL CALLBACK find_window(HWND window,LPARAM parameter)
{
    DWORD process_id = 0;
    wchar_t title[256]{};
    GetWindowThreadProcessId(window,&process_id);
    if(process_id != (DWORD)parameter || GetWindow(window,GW_OWNER) != nullptr) return TRUE;
    GetWindowTextW(window,title,(int)std::size(title));
    if(wcscmp(title,kGameTitle) == 0) {
        g_game.window = window;
        return FALSE;
    }
    return TRUE;
}

void refresh_window()
{
    if(g_game.process && (!g_game.window || !IsWindow(g_game.window))) {
        g_game.window = nullptr;
        EnumWindows(find_window,(LPARAM)g_game.process_id);
    }
}

std::wstring object_name(const wchar_t *kind)
{
    return L"Local\\AdventureIslandII" + std::wstring(kind) + L"-" + std::to_wstring(g_game.process_id);
}

bool signal_event(const wchar_t *kind)
{
    if(!g_game.process) return false;
    std::wstring name = object_name(kind);
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str());
    if(!event) return false;
    bool result = SetEvent(event) != FALSE;
    CloseHandle(event);
    return result;
}

enum class CommandKind { Save, Load, Capture, Audio };

volatile LONG *command_result(Ai2FrontendCommandState *state,CommandKind kind)
{
    if(!state)return nullptr;
    switch(kind) {
    case CommandKind::Save:return &state->save_result;
    case CommandKind::Load:return &state->load_result;
    case CommandKind::Capture:return &state->capture_result;
    case CommandKind::Audio:return &state->audio_result;
    }
    return nullptr;
}

bool send_command(const wchar_t *event_name,CommandKind kind)
{
    if(!g_game.process)return false;
    std::wstring mapping_name=object_name(L"CommandState");
    HANDLE mapping=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,mapping_name.c_str());
    if(!mapping)return false;
    auto *state=static_cast<Ai2FrontendCommandState *>(
        MapViewOfFile(mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Ai2FrontendCommandState)));
    if(!state){CloseHandle(mapping);return false;}
    volatile LONG *result=command_result(state,kind);
    InterlockedExchange(result,AI2_IPC_PENDING);
    if(!signal_event(event_name)){UnmapViewOfFile(state);CloseHandle(mapping);return false;}
    bool succeeded=false;
    for(unsigned attempt=0;attempt<250u;++attempt) {
        LONG value=InterlockedCompareExchange(result,AI2_IPC_PENDING,AI2_IPC_PENDING);
        if(value!=AI2_IPC_PENDING){succeeded=value==AI2_IPC_SUCCEEDED;break;}
        if(WaitForSingleObject(g_game.process,20)==WAIT_OBJECT_0)break;
    }
    UnmapViewOfFile(state);CloseHandle(mapping);return succeeded;
}

bool apply_audio_settings()
{
    if(!g_game.process || !g_next_ready) return false;
    std::wstring name = object_name(L"AudioConfig");
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,name.c_str());
    if(!mapping) return false;
    auto *config = static_cast<Ai2FrontendAudioConfig *>(
        MapViewOfFile(mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Ai2FrontendAudioConfig)));
    if(!config) { CloseHandle(mapping); return false; }
    InterlockedExchange(&config->enabled,g_next.audio_enabled ? 1 : 0);
    InterlockedExchange(&config->volume,g_next.volume_percent);
    InterlockedExchange(&config->latency_ms,g_next.audio_latency_ms);
    MemoryBarrier();
    UnmapViewOfFile(config);
    CloseHandle(mapping);
    return send_command(L"Audio",CommandKind::Audio);
}

void close_process_state()
{
    if(g_game.process) CloseHandle(g_game.process);
    g_game = {};
}

void focus_game()
{
    refresh_window();
    if(!g_game.window) return;
    AllowSetForegroundWindow(g_game.process_id);
    ShowWindow(g_game.window,SW_SHOW);
    BringWindowToTop(g_game.window);
    SetForegroundWindow(g_game.window);
    SetFocus(g_game.window);
}
}

void ai2_windows_live_settings_defaults(AI2LiveSettings *settings)
{
    if(!settings) return;
    std::memset(settings,0,sizeof(*settings));
    settings->correct_aspect=1;
    settings->pause_on_focus_loss=1;
    settings->audio_enabled=1;
    settings->volume_percent=70;
    settings->audio_latency_ms=60;
    settings->input_source=AI2_INPUT_COMBINED;
    settings->gamepad_deadzone_percent=35;
    ai2_gamepad_win32_default_bindings(settings->gamepad_bindings);
    const UINT p1[8]={VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,'D','F','S','A'};
    const UINT p2[8]={'W','S','A','D','I','U',VK_SHIFT,VK_TAB};
    std::memcpy(settings->bindings[0],p1,sizeof(p1));
    std::memcpy(settings->bindings[1],p2,sizeof(p2));
}

bool ai2_windows_live_set_next_settings(const AI2LiveSettings *settings)
{
    if(!settings) return false;
    g_next=*settings;
    g_next_ready=true;
    return !g_game.process||apply_audio_settings();
}

bool ai2_windows_live_start(HWND owner,const wchar_t *rom_path,wchar_t *error,size_t error_count)
{
    if(g_game.process || !rom_path || !*rom_path) return false;
    if(!g_next_ready) ai2_windows_live_settings_defaults(&g_next);
    g_next_ready=true;

    std::wstring executable=exe_path();
    std::wstring directory=exe_directory();
    std::wstring snapshots=directory+L"\\Snapshots";
    std::wstring screenshots=directory+L"\\Screenshots";
    CreateDirectoryW(snapshots.c_str(),nullptr);
    CreateDirectoryW(screenshots.c_str(),nullptr);

    std::wstring command=L"\""+executable+L"\" --game --rom";
    append_quoted(command,rom_path);
    command += L" --snapshot-dir"; append_quoted(command,snapshots.c_str());
    command += L" --screenshot-dir"; append_quoted(command,screenshots.c_str());
    append_number(command,L"--launcher-hwnd",(unsigned long long)(uintptr_t)owner);
    append_number(command,L"--scale",(unsigned)g_next.integer_scale);
    append_number(command,L"--volume",(unsigned)g_next.volume_percent);
    append_number(command,L"--audio-latency",(unsigned)g_next.audio_latency_ms);
    append_number(command,L"--input-source",(unsigned)g_next.input_source);
    append_number(command,L"--deadzone",(unsigned)g_next.gamepad_deadzone_percent);
    for(unsigned index=0; index<AI2_NES_BINDING_COUNT; ++index) {
        std::wstring option=L"--key-"+std::to_wstring(index);
        append_number(command,option.c_str(),(unsigned)g_next.bindings[0][index]);
        option=L"--pad-"+std::to_wstring(index);
        append_number(command,option.c_str(),(unsigned)g_next.gamepad_bindings[index]);
    }
    if(!g_next.audio_enabled) command += L" --mute";
    if(g_next.fullscreen) command += L" --fullscreen";
    if(g_next.correct_aspect) command += L" --correct-aspect";
    if(g_next.vsync) command += L" --vsync";
    if(g_next.pause_on_focus_loss) command += L" --pause-on-focus-loss";

    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
                             &security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    HANDLE output=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                              &security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(output==INVALID_HANDLE_VALUE || input==INVALID_HANDLE_VALUE) {
        if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);
        if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);
        if(error&&error_count)lstrcpynW(error,L"Windows could not prepare the game process.",(int)error_count);
        return false;
    }
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=input; startup.hStdOutput=output; startup.hStdError=output;
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutable_command(command.begin(),command.end()); mutable_command.push_back(0);
    BOOL created=CreateProcessW(executable.c_str(),mutable_command.data(),nullptr,nullptr,TRUE,
                                CREATE_NO_WINDOW,nullptr,directory.c_str(),&startup,&process);
    CloseHandle(output); CloseHandle(input);
    if(!created) {
        if(error&&error_count)lstrcpynW(error,L"Windows could not start the game process.",(int)error_count);
        return false;
    }
    CloseHandle(process.hThread);
    g_game.process=process.hProcess; g_game.process_id=process.dwProcessId;
    g_game.owner=owner; g_game.paused=false;
    for(unsigned attempt=0; attempt<750; ++attempt) {
        DWORD state=WaitForSingleObject(g_game.process,20);
        if(state==WAIT_OBJECT_0) {
            if(error&&error_count)lstrcpynW(error,L"The game process stopped during startup.",(int)error_count);
            close_process_state(); return false;
        }
        refresh_window();
        if(g_game.window) break;
    }
    if(!g_game.window) {
        if(error&&error_count)lstrcpynW(error,L"The game window did not appear.",(int)error_count);
        TerminateProcess(g_game.process,2); close_process_state(); return false;
    }
    focus_game();
    return true;
}

void ai2_windows_live_stop(void)
{
    if(!g_game.process) return;
    refresh_window();
    if(g_game.window) PostMessageW(g_game.window,WM_CLOSE,0,0);
    if(WaitForSingleObject(g_game.process,2000)!=WAIT_OBJECT_0) TerminateProcess(g_game.process,0);
    close_process_state();
}

bool ai2_windows_live_toggle_pause(void)
{
    if(!g_game.process)return false;
    if(g_game.paused&&signal_event(L"Resume")){g_game.paused=false;focus_game();return true;}
    if(!g_game.paused&&signal_event(L"Pause")){g_game.paused=true;return true;}
    return false;
}

void ai2_windows_live_resize(int,int,int,int) {}
void ai2_windows_live_key_event(UINT,WPARAM) {}
HANDLE ai2_windows_live_frame_timer(void) { return g_game.process; }

void ai2_windows_live_service_frame_timer(void)
{
    if(!g_game.process || WaitForSingleObject(g_game.process,0)!=WAIT_OBJECT_0) return;
    HWND owner=g_game.owner;
    close_process_state();
    if(owner&&IsWindow(owner)) PostMessageW(owner,AI2_WM_GAME_EXITED,0,0);
}

bool ai2_windows_live_quick_save(void) { return send_command(L"Save",CommandKind::Save); }
bool ai2_windows_live_quick_load(void) { return send_command(L"Load",CommandKind::Load); }
bool ai2_windows_live_take_screenshot(void) { return send_command(L"Capture",CommandKind::Capture); }
bool ai2_windows_live_is_running(void)
{
    if(!g_game.process) return false;
    DWORD code=0;
    return GetExitCodeProcess(g_game.process,&code) && code==STILL_ACTIVE;
}
bool ai2_windows_live_is_paused(void) { return !g_game.process || g_game.paused; }
void ai2_windows_live_mark_paused(void) { if(g_game.process)g_game.paused=true; }
#endif
