#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE

#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

#include "ai2_game_entry.h"

int WINAPI ai2_launcher_main(HINSTANCE instance, HINSTANCE previous,
                             LPWSTR command_line, int show);

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous,
                    LPWSTR command_line, int show)
{
    int count = 0;
    wchar_t **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if(arguments == NULL) return 2;
    if(count >= 2 && wcscmp(arguments[1], L"--game") == 0) {
        int result = ai2_game_main(count, arguments);
        LocalFree(arguments);
        return result;
    }
    LocalFree(arguments);
    return ai2_launcher_main(instance, previous, command_line, show);
}
