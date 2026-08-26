#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commdlg.h>
extern "C" {
#include "ai2_rom.h"
}
#include "ai2_windows_live.h"
#include "ai2_gamepad_input_win32.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>

#define AI2_COUNT(a) (sizeof(a)/sizeof((a)[0]))

namespace {
enum {
    CMD_BROWSE=100,CMD_RUN,CMD_PLAY,CMD_RESET,CMD_AUDIO,CMD_SETTINGS,CMD_KEYS,
    CMD_FULLSCREEN,CMD_AUTORUN,CMD_SAVE,CMD_LOAD,CMD_SCREENSHOT,
    CMD_ABOUT,CMD_EXIT,
    TOOL_BROWSE=200,TOOL_PLAY,TOOL_RESET,TOOL_AUDIO,TOOL_SETTINGS,TOOL_KEYS,
    TOOL_FULLSCREEN,TOOL_AUTORUN
};
HINSTANCE g_instance{};HWND g_main{},g_video{},g_browse{},g_play{},g_reset{},g_audio{},g_settings_button{},g_keys{},g_fullscreen{},g_autorun{},g_status{};
HWND g_settings_window{},g_audio_window{},g_bindings_window{},g_info_window{};HMENU g_menu{};std::wstring g_rom_path{};std::wstring g_ini_path{};
AI2LiveSettings g_settings{};bool g_auto_run{},g_hidden{},g_fullscreen_active{};
DWORD g_saved_style{},g_saved_ex_style{};WINDOWPLACEMENT g_saved_placement{sizeof(WINDOWPLACEMENT)};

std::wstring exe_directory(){wchar_t p[32768]={};DWORD n=GetModuleFileNameW(nullptr,p,(DWORD)AI2_COUNT(p));if(!n||n>=AI2_COUNT(p))return L".";std::wstring s(p,n);size_t slash=s.find_last_of(L"\\/");return slash==std::wstring::npos?L".":s.substr(0,slash);}
std::string narrow(const std::wstring &s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,nullptr,0,nullptr,nullptr);if(n<=0)return {};std::string r((size_t)n,'\0');WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,r.data(),n,nullptr,nullptr);r.pop_back();return r;}
std::wstring widen(const char *s){if(!s||!*s)return {};int n=MultiByteToWideChar(CP_UTF8,0,s,-1,nullptr,0);if(n<=0)return {};std::wstring r((size_t)n,L'\0');MultiByteToWideChar(CP_UTF8,0,s,-1,r.data(),n);r.pop_back();return r;}
void status(const wchar_t *text){if(!g_status)return;SetWindowTextW(g_status,text);NotifyWinEvent(EVENT_OBJECT_VALUECHANGE,g_status,OBJID_CLIENT,CHILDID_SELF);}
void font(HWND w){if(w)SendMessageW(w,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);}
bool frontend_key(UINT key){return key==VK_ESCAPE||(key>=VK_F1&&key<=VK_F8);}
bool valid_saved_key(UINT key){return key>0u&&key<255u&&!frontend_key(key);}

/* IsDialogMessage supplies standard Tab/Shift+Tab navigation. Route Tab
   explicitly so multiline read-only edit controls cannot retain focus. */
bool route_dialog_keyboard(HWND dialog,MSG *message){
    if(!dialog||!message||message->message!=WM_KEYDOWN||message->wParam!=VK_TAB)return dialog&&IsDialogMessageW(dialog,message);
    if(!IsWindow(dialog)||!IsWindowVisible(dialog)||!IsWindowEnabled(dialog))return false;
    HWND focused=GetFocus();
    if(!focused||(focused!=dialog&&!IsChild(dialog,focused)))return false;
    BOOL previous=GetKeyState(VK_SHIFT)<0?TRUE:FALSE;
    HWND next=GetNextDlgTabItem(dialog,focused,previous);
    if(!next||next==focused)return false;
    SetFocus(next);
    return true;
}

/* Apply the BattleTech navigation policy to the Welcome loop and every
   modeless Launcher dialog. */
#define IsDialogMessageW(dialog,message) route_dialog_keyboard((dialog),(message))

void defaults(){ai2_windows_live_settings_defaults(&g_settings);}
void load_settings(){
    defaults();g_ini_path=exe_directory()+L"\\settings.ini";
    g_settings.fullscreen=GetPrivateProfileIntW(L"Frontend",L"FullScreen",0,g_ini_path.c_str());
    g_settings.integer_scale=GetPrivateProfileIntW(L"Frontend",L"IntegerScale",0,g_ini_path.c_str());
    g_settings.correct_aspect=GetPrivateProfileIntW(L"Frontend",L"CorrectAspect",1,g_ini_path.c_str());
    g_settings.vsync=GetPrivateProfileIntW(L"Frontend",L"VSync",0,g_ini_path.c_str());
    g_settings.pause_on_focus_loss=GetPrivateProfileIntW(L"Frontend",L"PauseOnFocusLoss",1,g_ini_path.c_str());
    g_settings.audio_enabled=GetPrivateProfileIntW(L"Audio",L"Enabled",1,g_ini_path.c_str());
    g_settings.volume_percent=GetPrivateProfileIntW(L"Audio",L"Volume",70,g_ini_path.c_str());
    g_settings.audio_latency_ms=GetPrivateProfileIntW(L"Audio",L"LatencyMs",60,g_ini_path.c_str());
    g_settings.input_source=GetPrivateProfileIntW(L"Input",L"Source",AI2_INPUT_COMBINED,g_ini_path.c_str());
    if(g_settings.input_source<AI2_INPUT_KEYBOARD||g_settings.input_source>AI2_INPUT_COMBINED)g_settings.input_source=AI2_INPUT_COMBINED;
    g_settings.gamepad_deadzone_percent=GetPrivateProfileIntW(L"Input",L"GamepadDeadzonePercent",35,g_ini_path.c_str());
    if(g_settings.gamepad_deadzone_percent<10||g_settings.gamepad_deadzone_percent>90)g_settings.gamepad_deadzone_percent=35;
    if(g_settings.integer_scale<0||g_settings.integer_scale>4)g_settings.integer_scale=0;
    if(g_settings.volume_percent<0||g_settings.volume_percent>100)g_settings.volume_percent=70;
    if(g_settings.audio_latency_ms<20||g_settings.audio_latency_ms>2000)g_settings.audio_latency_ms=60;
    g_auto_run=GetPrivateProfileIntW(L"Frontend",L"AutoRun",0,g_ini_path.c_str())!=0;
    for(unsigned p=0;p<2;++p)for(unsigned a=0;a<8;++a){wchar_t key[32]={};_snwprintf(key,AI2_COUNT(key),L"P%uAction%u",p+1,a);UINT fallback=g_settings.bindings[p][a],value=(UINT)GetPrivateProfileIntW(L"Keyboard",key,fallback,g_ini_path.c_str());g_settings.bindings[p][a]=valid_saved_key(value)?value:fallback;}
    for(unsigned a=0;a<8;++a){wchar_t key[32]={};_snwprintf(key,AI2_COUNT(key),L"Action%u",a);int value=GetPrivateProfileIntW(L"Gamepad",key,g_settings.gamepad_bindings[a],g_ini_path.c_str());if(value>=AI2_GAMEPAD_DPAD_UP&&value<=AI2_GAMEPAD_CONTROL_LAST)g_settings.gamepad_bindings[a]=value;}
}
void write_number(const wchar_t *section,const wchar_t *key,int value){wchar_t text[24]={};_snwprintf(text,AI2_COUNT(text),L"%d",value);WritePrivateProfileStringW(section,key,text,g_ini_path.c_str());}
void save_settings(){
    write_number(L"Frontend",L"FullScreen",g_settings.fullscreen);write_number(L"Frontend",L"IntegerScale",g_settings.integer_scale);
    write_number(L"Frontend",L"CorrectAspect",g_settings.correct_aspect);write_number(L"Frontend",L"VSync",g_settings.vsync);
    write_number(L"Frontend",L"PauseOnFocusLoss",g_settings.pause_on_focus_loss);write_number(L"Frontend",L"AutoRun",g_auto_run?1:0);
    write_number(L"Audio",L"Enabled",g_settings.audio_enabled);write_number(L"Audio",L"Volume",g_settings.volume_percent);write_number(L"Audio",L"LatencyMs",g_settings.audio_latency_ms);
    write_number(L"Input",L"Source",g_settings.input_source);write_number(L"Input",L"GamepadDeadzonePercent",g_settings.gamepad_deadzone_percent);
    for(unsigned p=0;p<2;++p)for(unsigned a=0;a<8;++a){wchar_t key[32]={};_snwprintf(key,AI2_COUNT(key),L"P%uAction%u",p+1,a);write_number(L"Keyboard",key,(int)g_settings.bindings[p][a]);}
    for(unsigned a=0;a<8;++a){wchar_t key[32]={};_snwprintf(key,AI2_COUNT(key),L"Action%u",a);write_number(L"Gamepad",key,g_settings.gamepad_bindings[a]);}
}

bool validate_rom(const std::wstring &path,bool report){
    AI2Rom rom{};char error[256]={},reason[256]={};std::string p=narrow(path);
    if(!ai2_rom_load(p.c_str(),&rom,error,sizeof(error))){if(report)MessageBoxW(g_main,widen(error).c_str(),L"Adventure Island II ROM",MB_OK|MB_ICONERROR);return false;}
    bool ok=ai2_rom_is_expected(&rom,reason,sizeof(reason))!=0;ai2_rom_free(&rom);
    if(!ok){if(report)MessageBoxW(g_main,widen(reason).c_str(),L"Adventure Island II ROM",MB_OK|MB_ICONERROR);return false;}
    g_rom_path=path;status(L"Exact Adventure Island II (USA) ROM ready. Select Run or press F7.");return true;
}
bool try_default_rom(){
    std::wstring dir=exe_directory()+L"\\Rom";CreateDirectoryW(dir.c_str(),nullptr);
    WIN32_FIND_DATAW data{};HANDLE find=FindFirstFileW((dir+L"\\*.nes").c_str(),&data);if(find==INVALID_HANDLE_VALUE)return false;
    bool found=false;do{if(!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&validate_rom(dir+L"\\"+data.cFileName,false)){found=true;break;}}while(FindNextFileW(find,&data));FindClose(find);return found;
}
bool choose_rom(HWND owner){
    wchar_t path[32768]={};std::wstring initial=exe_directory()+L"\\Rom";OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=owner;ofn.lpstrTitle=L"Select Adventure Island II (USA) ROM";
    ofn.lpstrFilter=L"NES ROM (*.nes)\0*.nes\0All files (*.*)\0*.*\0";ofn.lpstrFile=path;ofn.nMaxFile=(DWORD)AI2_COUNT(path);ofn.lpstrInitialDir=initial.c_str();ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&ofn)&&validate_rom(path,true);
}

void show_information(HWND owner,const wchar_t *title,const wchar_t *body,int width,int height);
const wchar_t kWelcomeText[]=
    L"Welcome to Adventure Island II (NES) Static Recompilation\r\n\r\n"
    L"Frontend shortcuts\r\n"
    L"Escape - Switch between the game and Launcher\r\n"
    L"F1 - Welcome\r\n"
    L"F2 - Save snapshot\r\n"
    L"F3 - Load snapshot\r\n"
    L"F4 - Settings\r\n"
    L"F5 - Controller bindings\r\n"
    L"F6 - Audio settings\r\n"
    L"F7 - Run the selected ROM\r\n"
    L"F8 - Capture the complete game window\r\n\r\n"
    L"ROM title: Adventure Island II\r\nRegion: USA\r\nFile type: .nes\r\nDefault folder: Rom";
void controls(HWND owner){show_information(owner,L"Welcome",kWelcomeText,620,500);}

struct InfoData{HWND text{},close{},owner{};const wchar_t *body{};};
LRESULT CALLBACK InfoProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    InfoData *d=(InfoData*)GetWindowLongPtrW(w,GWLP_USERDATA);
    if(m==WM_CREATE){d=(InfoData*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)d);
        d->text=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",d->body,WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_LEFT|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,16,16,10,10,w,nullptr,g_instance,nullptr);
        d->close=CreateWindowW(L"BUTTON",L"&Close",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,0,0,100,32,w,(HMENU)IDOK,g_instance,nullptr);font(d->text);font(d->close);SetFocus(d->close);return 0;}
    if(m==WM_SIZE&&d){RECT r{};GetClientRect(w,&r);int width=std::max((int)r.right,1),height=std::max((int)r.bottom,1);MoveWindow(d->text,16,16,std::max(width-32,1),std::max(height-72,1),TRUE);MoveWindow(d->close,std::max((width-100)/2,0),std::max(height-44,0),100,32,TRUE);return 0;}
    if(m==WM_COMMAND&&(LOWORD(wp)==IDOK||LOWORD(wp)==IDCANCEL)){DestroyWindow(w);return 0;}
    if((m==WM_KEYDOWN&&wp==VK_ESCAPE)||m==WM_CLOSE){DestroyWindow(w);return 0;}
    if(m==WM_DESTROY&&d){if(d->owner&&IsWindow(d->owner)){EnableWindow(d->owner,TRUE);SetForegroundWindow(d->owner);}delete d;g_info_window=nullptr;return 0;}
    return DefWindowProcW(w,m,wp,lp);
}
void show_information(HWND owner,const wchar_t *title,const wchar_t *body,int width,int height){
    if(g_info_window){InfoData *d=(InfoData*)GetWindowLongPtrW(g_info_window,GWLP_USERDATA);if(d&&d->text){d->body=body;SetWindowTextW(d->text,body);}SetWindowTextW(g_info_window,title);ShowWindow(g_info_window,SW_SHOWNORMAL);BringWindowToTop(g_info_window);SetForegroundWindow(g_info_window);return;}
    WNDCLASSW wc{};wc.lpfnWndProc=InfoProc;wc.hInstance=g_instance;wc.lpszClassName=L"AI2InformationWindow";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);
    InfoData *d=new InfoData{};d->owner=owner;d->body=body;
    g_info_window=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_DLGMODALFRAME,wc.lpszClassName,title,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,width,height,owner,nullptr,g_instance,d);
    if(!g_info_window){delete d;return;}if(owner)EnableWindow(owner,FALSE);ShowWindow(g_info_window,SW_SHOWNORMAL);BringWindowToTop(g_info_window);SetForegroundWindow(g_info_window);
}
void welcome(HWND owner){show_information(owner,L"Welcome",
    kWelcomeText,620,500);}

struct SettingsData{HWND fullscreen,aspect,vsync,pause_focus,scale;};
int combo_value(const int *values,size_t count,int value){for(size_t i=0;i<count;++i)if(values[i]==value)return (int)i;return 0;}
LRESULT CALLBACK SettingsProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    static SettingsData d{};static const int scales[]={0,1,2,3,4};
    if(m==WM_CREATE){d={};CreateWindowW(L"STATIC",L"Video",WS_CHILD|WS_VISIBLE,18,14,200,22,w,nullptr,g_instance,nullptr);
        d.fullscreen=CreateWindowW(L"BUTTON",L"Start gameplay in &full screen",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,18,40,320,28,w,nullptr,g_instance,nullptr);
        d.aspect=CreateWindowW(L"BUTTON",L"Correct NES display to &4:3",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,18,70,320,28,w,nullptr,g_instance,nullptr);
        d.vsync=CreateWindowW(L"BUTTON",L"Use &vertical synchronization",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,18,100,320,28,w,nullptr,g_instance,nullptr);
        CreateWindowW(L"STATIC",L"&Scale:",WS_CHILD|WS_VISIBLE,362,44,70,22,w,nullptr,g_instance,nullptr);d.scale=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,430,40,150,180,w,nullptr,g_instance,nullptr);
        for(const wchar_t *s:{L"Fit window",L"1x",L"2x",L"3x",L"4x"})SendMessageW(d.scale,CB_ADDSTRING,0,(LPARAM)s);
        d.pause_focus=CreateWindowW(L"BUTTON",L"&Pause when the game loses focus",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,18,146,390,28,w,nullptr,g_instance,nullptr);
        CreateWindowW(L"STATIC",L"Changes apply the next time gameplay starts.",WS_CHILD|WS_VISIBLE,18,186,562,24,w,nullptr,g_instance,nullptr);
        CreateWindowW(L"BUTTON",L"OK",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,352,226,105,32,w,(HMENU)IDOK,g_instance,nullptr);CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,475,226,105,32,w,(HMENU)IDCANCEL,g_instance,nullptr);
        SendMessageW(d.fullscreen,BM_SETCHECK,g_settings.fullscreen?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(d.aspect,BM_SETCHECK,g_settings.correct_aspect?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(d.vsync,BM_SETCHECK,g_settings.vsync?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(d.pause_focus,BM_SETCHECK,g_settings.pause_on_focus_loss?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(d.scale,CB_SETCURSEL,combo_value(scales,AI2_COUNT(scales),g_settings.integer_scale),0);SetFocus(d.fullscreen);return 0;}
    if(m==WM_COMMAND){if(LOWORD(wp)==IDOK){g_settings.fullscreen=SendMessageW(d.fullscreen,BM_GETCHECK,0,0)==BST_CHECKED;g_settings.correct_aspect=SendMessageW(d.aspect,BM_GETCHECK,0,0)==BST_CHECKED;g_settings.vsync=SendMessageW(d.vsync,BM_GETCHECK,0,0)==BST_CHECKED;g_settings.pause_on_focus_loss=SendMessageW(d.pause_focus,BM_GETCHECK,0,0)==BST_CHECKED;int s=(int)SendMessageW(d.scale,CB_GETCURSEL,0,0);if(s>=0&&s<(int)AI2_COUNT(scales))g_settings.integer_scale=scales[s];save_settings();status(L"Settings saved. They apply when gameplay next starts.");DestroyWindow(w);return 0;}if(LOWORD(wp)==IDCANCEL){DestroyWindow(w);return 0;}}
    if((m==WM_KEYDOWN&&wp==VK_ESCAPE)||m==WM_CLOSE){DestroyWindow(w);return 0;}if(m==WM_DESTROY&&w==g_settings_window){g_settings_window=nullptr;if(g_main&&IsWindow(g_main)){EnableWindow(g_main,TRUE);SetForegroundWindow(g_main);}return 0;}return DefWindowProcW(w,m,wp,lp);
}
void open_settings(HWND owner){if(g_settings_window){ShowWindow(g_settings_window,SW_SHOW);SetForegroundWindow(g_settings_window);return;}WNDCLASSW wc{};wc.lpfnWndProc=SettingsProc;wc.hInstance=g_instance;wc.lpszClassName=L"AI2ModernSettings";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);g_settings_window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Settings",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,620,320,owner,nullptr,g_instance,nullptr);if(g_settings_window)EnableWindow(owner,FALSE);}

struct AudioData{HWND enabled{},volume{},latency{};};
LRESULT CALLBACK AudioProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    static AudioData d{};static const int volumes[]={0,25,50,70,100},latencies[]={20,40,60,80,120,250};
    if(m==WM_CREATE){d={};CreateWindowW(L"STATIC",L"Audio output",WS_CHILD|WS_VISIBLE,18,14,260,22,w,nullptr,g_instance,nullptr);
        d.enabled=CreateWindowW(L"BUTTON",L"Enable &audio output",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,18,42,260,28,w,nullptr,g_instance,nullptr);
        CreateWindowW(L"STATIC",L"&Volume:",WS_CHILD|WS_VISIBLE,18,88,86,22,w,nullptr,g_instance,nullptr);d.volume=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,110,84,120,170,w,nullptr,g_instance,nullptr);
        for(const wchar_t *s:{L"Muted",L"25%",L"50%",L"70%",L"100%"})SendMessageW(d.volume,CB_ADDSTRING,0,(LPARAM)s);
        CreateWindowW(L"STATIC",L"&Latency:",WS_CHILD|WS_VISIBLE,18,128,86,22,w,nullptr,g_instance,nullptr);d.latency=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,110,124,120,190,w,nullptr,g_instance,nullptr);
        for(const wchar_t *s:{L"20 ms",L"40 ms",L"60 ms",L"80 ms",L"120 ms",L"250 ms"})SendMessageW(d.latency,CB_ADDSTRING,0,(LPARAM)s);
        CreateWindowW(L"STATIC",L"Audio changes apply the next time gameplay starts.",WS_CHILD|WS_VISIBLE,18,170,390,24,w,nullptr,g_instance,nullptr);
        CreateWindowW(L"BUTTON",L"OK",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,226,210,105,32,w,(HMENU)IDOK,g_instance,nullptr);CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,349,210,105,32,w,(HMENU)IDCANCEL,g_instance,nullptr);
        SendMessageW(d.enabled,BM_SETCHECK,g_settings.audio_enabled?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(d.volume,CB_SETCURSEL,combo_value(volumes,AI2_COUNT(volumes),g_settings.volume_percent),0);SendMessageW(d.latency,CB_SETCURSEL,combo_value(latencies,AI2_COUNT(latencies),g_settings.audio_latency_ms),0);SetFocus(d.enabled);return 0;}
    if(m==WM_COMMAND){if(LOWORD(wp)==IDOK){g_settings.audio_enabled=SendMessageW(d.enabled,BM_GETCHECK,0,0)==BST_CHECKED;int v=(int)SendMessageW(d.volume,CB_GETCURSEL,0,0),l=(int)SendMessageW(d.latency,CB_GETCURSEL,0,0);if(v>=0&&v<(int)AI2_COUNT(volumes))g_settings.volume_percent=volumes[v];if(l>=0&&l<(int)AI2_COUNT(latencies))g_settings.audio_latency_ms=latencies[l];save_settings();bool applied=ai2_windows_live_set_next_settings(&g_settings);status(!ai2_windows_live_is_running()?L"Audio settings saved for the next game.":applied?L"Audio settings applied to the running game.":L"Audio settings were saved, but the running game could not apply them.");DestroyWindow(w);return 0;}if(LOWORD(wp)==IDCANCEL){DestroyWindow(w);return 0;}}
    if((m==WM_KEYDOWN&&wp==VK_ESCAPE)||m==WM_CLOSE){DestroyWindow(w);return 0;}if(m==WM_DESTROY&&w==g_audio_window){g_audio_window=nullptr;if(g_main&&IsWindow(g_main)){EnableWindow(g_main,TRUE);SetForegroundWindow(g_main);}return 0;}return DefWindowProcW(w,m,wp,lp);
}
void open_audio(HWND owner){if(g_audio_window){ShowWindow(g_audio_window,SW_SHOW);SetForegroundWindow(g_audio_window);return;}WNDCLASSW wc{};wc.lpfnWndProc=AudioProc;wc.hInstance=g_instance;wc.lpszClassName=L"AI2ModernAudio";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);g_audio_window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Audio",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,490,300,owner,nullptr,g_instance,nullptr);if(g_audio_window)EnableWindow(owner,FALSE);}

const wchar_t *action_name(unsigned a){static const wchar_t *names[8]={L"Up",L"Down",L"Left",L"Right",L"B",L"A",L"Start",L"Select"};return a<8?names[a]:L"Unknown";}
std::wstring key_name(UINT key){switch(key){case VK_UP:return L"Up Arrow";case VK_DOWN:return L"Down Arrow";case VK_LEFT:return L"Left Arrow";case VK_RIGHT:return L"Right Arrow";case VK_TAB:return L"Tab";case VK_SHIFT:return L"Shift";default:break;}UINT scan=MapVirtualKeyW(key,MAPVK_VK_TO_VSC);wchar_t name[64]={};if(GetKeyNameTextW((LONG)(scan<<16),name,(int)AI2_COUNT(name))>0)return name;return L"Key "+std::to_wstring(key);}
struct BindData{HWND button[8]{},source{},deadzone{};UINT pending[2][8]{};int action{-1};};
void binding_text(BindData &d,unsigned a){std::wstring text=std::wstring(action_name(a))+L": "+key_name(d.pending[0][a]);SetWindowTextW(d.button[a],text.c_str());}
LRESULT CALLBACK BindProc(HWND w,UINT m,WPARAM wp,LPARAM lp){static BindData d{};constexpr int base=5000;
    if(m==WM_CREATE){d={};std::memcpy(d.pending,g_settings.bindings,sizeof(d.pending));CreateWindowW(L"STATIC",L"Input source:",WS_CHILD|WS_VISIBLE,18,14,90,22,w,nullptr,g_instance,nullptr);d.source=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,110,10,205,120,w,(HMENU)4900,g_instance,nullptr);for(const wchar_t *s:{L"Keyboard",L"Gamepad",L"Keyboard + gamepad"})SendMessageW(d.source,CB_ADDSTRING,0,(LPARAM)s);SendMessageW(d.source,CB_SETCURSEL,g_settings.input_source,0);
        CreateWindowW(L"STATIC",L"Deadzone:",WS_CHILD|WS_VISIBLE,350,14,80,22,w,nullptr,g_instance,nullptr);d.deadzone=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,438,10,120,120,w,(HMENU)4901,g_instance,nullptr);for(const wchar_t *s:{L"20%",L"35%",L"50%",L"65%"})SendMessageW(d.deadzone,CB_ADDSTRING,0,(LPARAM)s);int dead_index=g_settings.gamepad_deadzone_percent<=20?0:g_settings.gamepad_deadzone_percent<=35?1:g_settings.gamepad_deadzone_percent<=50?2:3;SendMessageW(d.deadzone,CB_SETCURSEL,dead_index,0);
        CreateWindowW(L"STATIC",L"Gamepad: D-pad or left stick; west button = B; south button = A; Start = Start; Back = Select. Controllers may be connected during play.",WS_CHILD|WS_VISIBLE,18,48,540,42,w,nullptr,g_instance,nullptr);CreateWindowW(L"STATIC",L"Player 1 keyboard",WS_CHILD|WS_VISIBLE,18,96,540,24,w,nullptr,g_instance,nullptr);
        for(unsigned a=0;a<8;++a){int id=base+(int)a;d.button[a]=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP,18,126+(int)a*38,540,30,w,(HMENU)(INT_PTR)id,g_instance,nullptr);binding_text(d,a);}
        CreateWindowW(L"STATIC",L"Choose a keyboard control, then press its new key. Escape cancels capture. Launcher shortcuts cannot be assigned.",WS_CHILD|WS_VISIBLE,18,438,540,42,w,nullptr,g_instance,nullptr);CreateWindowW(L"BUTTON",L"OK",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,330,492,105,32,w,(HMENU)IDOK,g_instance,nullptr);CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,453,492,105,32,w,(HMENU)IDCANCEL,g_instance,nullptr);return 0;}
    if(m==WM_COMMAND){int id=LOWORD(wp);if(id>=base&&id<base+8){d.action=id-base;SetWindowTextW(d.button[d.action],L"Press a key...");SetFocus(w);return 0;}if(id==IDOK){static const int deadzones[4]={20,35,50,65};int source=(int)SendMessageW(d.source,CB_GETCURSEL,0,0),deadzone=(int)SendMessageW(d.deadzone,CB_GETCURSEL,0,0);if(source>=AI2_INPUT_KEYBOARD&&source<=AI2_INPUT_COMBINED)g_settings.input_source=source;if(deadzone>=0&&deadzone<4)g_settings.gamepad_deadzone_percent=deadzones[deadzone];std::memcpy(g_settings.bindings,d.pending,sizeof(d.pending));save_settings();DestroyWindow(w);return 0;}if(id==IDCANCEL){DestroyWindow(w);return 0;}}
    if(m==WM_KEYDOWN&&d.action>=0){UINT key=(UINT)wp;if(key==VK_ESCAPE){binding_text(d,(unsigned)d.action);d.action=-1;return 0;}if(frontend_key(key)){MessageBeep(MB_ICONWARNING);return 0;}for(unsigned a=0;a<8;++a)if((int)a!=d.action&&d.pending[0][a]==key){d.pending[0][a]=d.pending[0][d.action];binding_text(d,a);}d.pending[0][d.action]=key;binding_text(d,(unsigned)d.action);d.action=-1;return 0;}
    if((m==WM_KEYDOWN&&wp==VK_ESCAPE)||m==WM_CLOSE){DestroyWindow(w);return 0;}if(m==WM_DESTROY&&w==g_bindings_window){g_bindings_window=nullptr;if(g_main&&IsWindow(g_main)){EnableWindow(g_main,TRUE);SetForegroundWindow(g_main);}return 0;}return DefWindowProcW(w,m,wp,lp);
}
void open_bindings(HWND owner){if(g_bindings_window){ShowWindow(g_bindings_window,SW_SHOW);SetForegroundWindow(g_bindings_window);return;}WNDCLASSW wc{};wc.lpfnWndProc=BindProc;wc.hInstance=g_instance;wc.lpszClassName=L"AI2ModernBindings";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);g_bindings_window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Controls",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,590,585,owner,nullptr,g_instance,nullptr);if(g_bindings_window)EnableWindow(owner,FALSE);}

void visible(bool show){int c=show?SW_SHOW:SW_HIDE;for(HWND w:{g_browse,g_play,g_reset,g_audio,g_settings_button,g_keys,g_fullscreen,g_autorun,g_status})ShowWindow(w,c);}
void layout(HWND w){RECT r{};GetClientRect(w,&r);int width=std::max((int)r.right,1),height=std::max((int)r.bottom,1),top=g_hidden?0:80;
    if(!g_hidden){MoveWindow(g_browse,8,8,92,30,TRUE);MoveWindow(g_play,106,8,72,30,TRUE);MoveWindow(g_reset,184,8,66,30,TRUE);MoveWindow(g_audio,256,8,66,30,TRUE);MoveWindow(g_settings_button,328,8,82,30,TRUE);MoveWindow(g_keys,416,8,84,30,TRUE);MoveWindow(g_fullscreen,510,10,112,26,TRUE);MoveWindow(g_autorun,632,10,100,26,TRUE);MoveWindow(g_status,12,48,std::max(width-24,1),24,TRUE);}MoveWindow(g_video,0,top,width,std::max(height-top,1),TRUE);ai2_windows_live_resize(0,top,width,std::max(height-top,1));}
void update(){bool running=ai2_windows_live_is_running(),can_play=running&&ai2_windows_live_is_paused();EnableWindow(g_browse,!running);EnableWindow(g_play,can_play);EnableWindow(g_reset,running);SetWindowTextW(g_browse,g_rom_path.empty()?L"&Browse":L"&Run");SetWindowTextW(g_play,L"&Play");SendMessageW(g_fullscreen,BM_SETCHECK,g_settings.fullscreen?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(g_autorun,BM_SETCHECK,g_auto_run?BST_CHECKED:BST_UNCHECKED,0);ShowWindow(g_video,SW_SHOW);
    EnableMenuItem(g_menu,CMD_BROWSE,MF_BYCOMMAND|(running?MF_GRAYED:MF_ENABLED));EnableMenuItem(g_menu,CMD_RUN,MF_BYCOMMAND|(!running&&!g_rom_path.empty()?MF_ENABLED:MF_GRAYED));EnableMenuItem(g_menu,CMD_PLAY,MF_BYCOMMAND|(running?MF_ENABLED:MF_GRAYED));EnableMenuItem(g_menu,CMD_RESET,MF_BYCOMMAND|(running?MF_ENABLED:MF_GRAYED));EnableMenuItem(g_menu,CMD_SAVE,MF_BYCOMMAND|(running?MF_ENABLED:MF_GRAYED));EnableMenuItem(g_menu,CMD_LOAD,MF_BYCOMMAND|(running?MF_ENABLED:MF_GRAYED));EnableMenuItem(g_menu,CMD_SCREENSHOT,MF_BYCOMMAND|(running?MF_ENABLED:MF_GRAYED));CheckMenuItem(g_menu,CMD_FULLSCREEN,MF_BYCOMMAND|(g_settings.fullscreen?MF_CHECKED:MF_UNCHECKED));CheckMenuItem(g_menu,CMD_AUTORUN,MF_BYCOMMAND|(g_auto_run?MF_CHECKED:MF_UNCHECKED));DrawMenuBar(g_main);}
void launcher_view(HWND w){g_hidden=false;visible(true);SetMenu(w,g_menu);SetWindowTextW(w,L"Launcher");ShowWindow(w,SW_RESTORE);BringWindowToTop(w);SetForegroundWindow(w);layout(w);DrawMenuBar(w);InvalidateRect(w,nullptr,TRUE);}
void game_view(HWND w){g_hidden=true;ShowWindow(w,SW_HIDE);}
bool start(HWND w){if(g_rom_path.empty()){MessageBoxW(w,L"Place Adventure Island II (USA).nes in the Rom folder or select Browse ROM.",L"Adventure Island II ROM",MB_OK|MB_ICONINFORMATION);return false;}wchar_t error[512]={};ai2_windows_live_set_next_settings(&g_settings);if(!ai2_windows_live_start(w,g_rom_path.c_str(),error,AI2_COUNT(error))){MessageBoxW(w,error[0]?error:L"The Adventure Island II static core could not start.",L"Play",MB_OK|MB_ICONERROR);return false;}layout(w);update();status(L"Adventure Island II is running at native NTSC timing. Press Escape to return to Launcher.");game_view(w);return true;}
void toggle_play(HWND w){if(!ai2_windows_live_is_running())return;if(ai2_windows_live_is_paused()){if(!ai2_windows_live_toggle_pause()){status(L"The game could not resume.");return;}status(L"Adventure Island II resumed. Press Escape to return to Launcher.");update();game_view(w);}else{if(!ai2_windows_live_toggle_pause()){status(L"The game could not pause.");return;}launcher_view(w);status(L"Paused. Choose Play or press Escape to continue.");update();SetFocus(g_play);NotifyWinEvent(EVENT_OBJECT_FOCUS,g_play,OBJID_CLIENT,CHILDID_SELF);}}
void reset_game(HWND w){if(ai2_windows_live_is_running()){ai2_windows_live_stop();if(start(w))status(L"Adventure Island II reset from frame 0.");}}

LRESULT CALLBACK MainProc(HWND w,UINT m,WPARAM wp,LPARAM lp){switch(m){
    case WM_CREATE:g_main=w;g_video=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"",WS_CHILD|WS_VISIBLE|WS_DISABLED|SS_BLACKRECT,0,80,1,1,w,nullptr,g_instance,nullptr);g_browse=CreateWindowW(L"BUTTON",L"&Browse",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,8,8,92,30,w,(HMENU)TOOL_BROWSE,g_instance,nullptr);g_play=CreateWindowW(L"BUTTON",L"&Play",WS_CHILD|WS_VISIBLE|WS_TABSTOP,106,8,72,30,w,(HMENU)TOOL_PLAY,g_instance,nullptr);g_reset=CreateWindowW(L"BUTTON",L"&Reset",WS_CHILD|WS_VISIBLE|WS_TABSTOP,184,8,66,30,w,(HMENU)TOOL_RESET,g_instance,nullptr);g_audio=CreateWindowW(L"BUTTON",L"&Audio",WS_CHILD|WS_VISIBLE|WS_TABSTOP,256,8,66,30,w,(HMENU)TOOL_AUDIO,g_instance,nullptr);g_settings_button=CreateWindowW(L"BUTTON",L"&Settings",WS_CHILD|WS_VISIBLE|WS_TABSTOP,328,8,82,30,w,(HMENU)TOOL_SETTINGS,g_instance,nullptr);g_keys=CreateWindowW(L"BUTTON",L"&Controls",WS_CHILD|WS_VISIBLE|WS_TABSTOP,416,8,84,30,w,(HMENU)TOOL_KEYS,g_instance,nullptr);g_fullscreen=CreateWindowW(L"BUTTON",L"&Full screen",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,510,10,112,26,w,(HMENU)TOOL_FULLSCREEN,g_instance,nullptr);g_autorun=CreateWindowW(L"BUTTON",L"Auto-&Run",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,632,10,100,26,w,(HMENU)TOOL_AUTORUN,g_instance,nullptr);g_status=CreateWindowW(L"STATIC",L"Place Adventure Island II (USA).nes in the portable Rom folder.",WS_CHILD|WS_VISIBLE|SS_LEFT|SS_NOPREFIX,12,48,800,24,w,nullptr,g_instance,nullptr);for(HWND c:{g_browse,g_play,g_reset,g_audio,g_settings_button,g_keys,g_fullscreen,g_autorun,g_status})font(c);update();return 0;
    case WM_SIZE:layout(w);return 0;case WM_GETMINMAXINFO:((MINMAXINFO*)lp)->ptMinTrackSize={760,580};return 0;
    case WM_COMMAND:{unsigned id=LOWORD(wp);if(id==CMD_BROWSE||(id==TOOL_BROWSE&&g_rom_path.empty())){if(choose_rom(w)){if(ai2_windows_live_is_running())ai2_windows_live_stop();update();}return 0;}if(id==CMD_RUN||(id==TOOL_BROWSE&&!g_rom_path.empty())){if(!ai2_windows_live_is_running())start(w);return 0;}if(id==TOOL_PLAY||id==CMD_PLAY){toggle_play(w);return 0;}if(id==TOOL_RESET||id==CMD_RESET){reset_game(w);return 0;}if(id==TOOL_AUDIO||id==CMD_AUDIO){open_audio(w);return 0;}if(id==TOOL_SETTINGS||id==CMD_SETTINGS){open_settings(w);return 0;}if(id==TOOL_KEYS||id==CMD_KEYS){open_bindings(w);return 0;}if(id==TOOL_FULLSCREEN||id==CMD_FULLSCREEN){if(id==CMD_FULLSCREEN)SendMessageW(g_fullscreen,BM_SETCHECK,SendMessageW(g_fullscreen,BM_GETCHECK,0,0)==BST_CHECKED?BST_UNCHECKED:BST_CHECKED,0);g_settings.fullscreen=SendMessageW(g_fullscreen,BM_GETCHECK,0,0)==BST_CHECKED;save_settings();update();return 0;}if(id==TOOL_AUTORUN||id==CMD_AUTORUN){if(id==CMD_AUTORUN)SendMessageW(g_autorun,BM_SETCHECK,SendMessageW(g_autorun,BM_GETCHECK,0,0)==BST_CHECKED?BST_UNCHECKED:BST_CHECKED,0);g_auto_run=SendMessageW(g_autorun,BM_GETCHECK,0,0)==BST_CHECKED;save_settings();update();status(g_auto_run?L"Auto-Run enabled.":L"Auto-Run disabled.");return 0;}if(id==CMD_SAVE){if(!ai2_windows_live_quick_save())MessageBoxW(w,L"The snapshot could not be saved.",L"Save Snapshot",MB_OK|MB_ICONERROR);return 0;}if(id==CMD_LOAD){if(!ai2_windows_live_quick_load())MessageBoxW(w,L"No compatible snapshot was found.",L"Load Snapshot",MB_OK|MB_ICONINFORMATION);return 0;}if(id==CMD_SCREENSHOT){if(!ai2_windows_live_take_screenshot())MessageBoxW(w,L"The game-window screenshot could not be saved.",L"Capture Game Window",MB_OK|MB_ICONERROR);return 0;}if(id==CMD_ABOUT){show_information(w,L"About Adventure Island II",L"F1 - Welcome\r\n\r\nAdventure Island II NES Static Recompilation\r\n\r\nTitle: Adventure Island II\r\nRegion: USA\r\nFile type: .nes",520,300);return 0;}if(id==CMD_EXIT){DestroyWindow(w);return 0;}break;}
    case WM_KEYDOWN:if(wp==VK_ESCAPE){if((lp&(1L<<30))==0)toggle_play(w);return 0;}if(wp==VK_F1){if(g_hidden)toggle_play(w);controls(w);return 0;}if(wp==VK_F2){SendMessageW(w,WM_COMMAND,CMD_SAVE,0);return 0;}if(wp==VK_F3){SendMessageW(w,WM_COMMAND,CMD_LOAD,0);return 0;}if(wp==VK_F4){if(g_hidden)toggle_play(w);open_settings(w);return 0;}if(wp==VK_F5){if(g_hidden)toggle_play(w);open_bindings(w);return 0;}if(wp==VK_F6){if(g_hidden)toggle_play(w);open_audio(w);return 0;}if(wp==VK_F7){if(!ai2_windows_live_is_running())SendMessageW(w,WM_COMMAND,CMD_RUN,0);return 0;}if(wp==VK_F8){SendMessageW(w,WM_COMMAND,CMD_SCREENSHOT,0);return 0;}if(ai2_windows_live_is_running()&&!ai2_windows_live_is_paused()){ai2_windows_live_key_event(WM_KEYDOWN,wp);return 0;}break;
    case WM_KEYUP:if(ai2_windows_live_is_running()&&!ai2_windows_live_is_paused()){ai2_windows_live_key_event(WM_KEYUP,wp);return 0;}break;
    case WM_KILLFOCUS:return 0;
    case AI2_WM_GAME_RETURN:ai2_windows_live_mark_paused();launcher_view(w);status(L"Paused. Choose Play or press Escape to continue.");update();SetFocus(g_play);NotifyWinEvent(EVENT_OBJECT_FOCUS,g_play,OBJID_CLIENT,CHILDID_SELF);return 0;
    case AI2_WM_GAME_EXITED:launcher_view(w);status(L"Game closed.");update();return 0;
    case WM_CLOSE:if(ai2_windows_live_is_running()&&MessageBoxW(w,L"The game is still running. Exit both the game and Launcher?",L"Launcher",MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;ai2_windows_live_stop();DestroyWindow(w);return 0;case WM_DESTROY:PostQuitMessage(0);return 0;default:break;}return DefWindowProcW(w,m,wp,lp);}

HMENU make_menu(){HMENU bar=CreateMenu(),file=CreatePopupMenu(),settings=CreatePopupMenu();AppendMenuW(file,MF_STRING,CMD_BROWSE,L"&Browse ROM...");AppendMenuW(file,MF_STRING,CMD_RUN,L"&Run\tF7");AppendMenuW(file,MF_STRING,CMD_PLAY,L"&Play\tEscape");AppendMenuW(file,MF_STRING,CMD_RESET,L"&Reset ROM");AppendMenuW(file,MF_SEPARATOR,0,nullptr);AppendMenuW(file,MF_STRING,CMD_SAVE,L"Save Snapshot\tF2");AppendMenuW(file,MF_STRING,CMD_LOAD,L"Load Snapshot\tF3");AppendMenuW(file,MF_STRING,CMD_SCREENSHOT,L"Capture Game Frame\tF8");AppendMenuW(file,MF_SEPARATOR,0,nullptr);AppendMenuW(file,MF_STRING,CMD_EXIT,L"E&xit\tAlt+F4");AppendMenuW(settings,MF_STRING,CMD_SETTINGS,L"&Settings...\tF4");AppendMenuW(settings,MF_STRING,CMD_KEYS,L"&Controls...\tF5");AppendMenuW(settings,MF_STRING,CMD_AUDIO,L"&Audio...\tF6");AppendMenuW(settings,MF_SEPARATOR,0,nullptr);AppendMenuW(settings,MF_STRING,CMD_FULLSCREEN,L"Use &Full Screen When Playing");AppendMenuW(settings,MF_STRING,CMD_AUTORUN,L"&Auto-Run at Startup");AppendMenuW(settings,MF_SEPARATOR,0,nullptr);AppendMenuW(settings,MF_STRING,CMD_ABOUT,L"&About");AppendMenuW(bar,MF_POPUP,(UINT_PTR)file,L"&File");AppendMenuW(bar,MF_POPUP,(UINT_PTR)settings,L"&Settings");return bar;}
}

int WINAPI ai2_launcher_main(HINSTANCE h,HINSTANCE,LPWSTR,int){g_instance=h;load_settings();MSG welcome_message{};if(!GetPrivateProfileIntW(L"Frontend",L"WelcomeShown",0,g_ini_path.c_str())){WritePrivateProfileStringW(L"Frontend",L"WelcomeShown",L"1",g_ini_path.c_str());welcome(nullptr);while(g_info_window&&GetMessageW(&welcome_message,nullptr,0,0)>0){if(g_info_window&&IsDialogMessageW(g_info_window,&welcome_message))continue;TranslateMessage(&welcome_message);DispatchMessageW(&welcome_message);}}g_menu=make_menu();WNDCLASSW wc{};wc.lpfnWndProc=MainProc;wc.hInstance=h;wc.lpszClassName=L"AI2ModernLauncher";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){MessageBoxW(nullptr,L"Windows could not register the Launcher window class.",L"Launcher startup error",MB_OK|MB_ICONERROR);return 1;}HWND w=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Launcher",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1024,820,nullptr,g_menu,h,nullptr);if(!w)return 1;bool found=try_default_rom();update();ShowWindow(w,SW_SHOWNORMAL);UpdateWindow(w);BringWindowToTop(w);SetForegroundWindow(w);if(found&&g_auto_run)PostMessageW(w,WM_COMMAND,CMD_RUN,0);
    MSG msg{};bool running=true;while(running){HANDLE timer=ai2_windows_live_frame_timer();DWORD count=timer?1u:0u;DWORD result=MsgWaitForMultipleObjectsEx(count,timer?&timer:nullptr,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);bool timer_ready=timer&&result==WAIT_OBJECT_0;if(result==WAIT_FAILED)break;
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){running=false;break;}if(g_info_window&&IsDialogMessageW(g_info_window,&msg))continue;if(g_settings_window&&IsDialogMessageW(g_settings_window,&msg))continue;if(g_audio_window&&IsDialogMessageW(g_audio_window,&msg))continue;if(g_bindings_window&&IsDialogMessageW(g_bindings_window,&msg))continue;bool root=msg.message==WM_KEYDOWN&&frontend_key((UINT)msg.wParam);if(root){SendMessageW(w,msg.message,msg.wParam,msg.lParam);continue;}if(g_hidden||!IsDialogMessageW(w,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        HANDLE current=ai2_windows_live_frame_timer();if(timer_ready&&current==timer)ai2_windows_live_service_frame_timer();}
    return (int)msg.wParam;}
