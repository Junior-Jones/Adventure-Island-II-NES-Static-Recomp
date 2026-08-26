cmake_minimum_required(VERSION 3.20)
project(AdventureIslandII_NES_Static_Recomp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/generated/v05/generated_sources.cmake)

set(AI2_CORE_SOURCES
    src/api/ai2_sha256.c
    src/api/ai2_v09_core.c
    src/apu/ai2_v08_apu.c
    src/mapper/ai2_mmc3_mapper.c
    src/ppu/ai2_v07_ppu.c
    src/runtime/ai2_v06_runtime.c
    src/runtime/ai2_v06_semantics.c
    ${AI2_V05_GENERATED_SOURCES})

add_library(ai2-static-core STATIC ${AI2_CORE_SOURCES})
target_include_directories(ai2-static-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/v05
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mapper
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ppu
    ${CMAKE_CURRENT_SOURCE_DIR}/src/apu)
if(NOT WIN32)
    target_compile_definitions(ai2-static-core PRIVATE _POSIX_C_SOURCE=200809L)
endif()

add_library(ai2-frontend-common STATIC
    frontend/common/ai2_frontend_session.c
    frontend/common/ai2_frontend_input.c
    frontend/common/ai2_frontend_capture.c)
target_include_directories(ai2-frontend-common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ai2-frontend-common PUBLIC ai2-static-core)

option(AI2_BUILD_DEVELOPER_TOOLS "Build headless developer tools" OFF)
if(AI2_BUILD_DEVELOPER_TOOLS)
    add_executable(ai2-headless frontend/headless/ai2_headless.c)
    target_link_libraries(ai2-headless PRIVATE ai2-frontend-common)
endif()

if(WIN32)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(ai2_sdl
        URL "https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.10.zip"
        URL_HASH "SHA256=30DCE54890B17544A35E4521274762016AF6ED59C2183B896EF5E6693E97FB6C"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(ai2_sdl)

    add_executable(adventure-island-ii-launcher WIN32
        frontend/windows/ai2_windows_entry.cpp
        frontend/windows/ai2_version.rc
        frontend/windows/ai2_windows_launcher.cpp
        frontend/windows/ai2_windows_live.cpp
        frontend/windows/ai2_frontend_sdl3.cpp
        frontend/windows/ai2_audio_output_sdl3.cpp
        frontend/windows/ai2_rom.cpp
        third_party/ai2-gamepad/ai2_gamepad_input_win32.c)
    target_include_directories(adventure-island-ii-launcher PRIVATE
        frontend/windows
        third_party/ai2-gamepad
        src/api)
    target_compile_definitions(adventure-island-ii-launcher PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(adventure-island-ii-launcher PRIVATE
        ai2-static-core SDL3::SDL3-static comdlg32 user32 gdi32 ole32 shell32)
    set_target_properties(adventure-island-ii-launcher PROPERTIES
        OUTPUT_NAME "Launcher"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/release")
    add_custom_command(TARGET adventure-island-ii-launcher POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/SDL-LICENSE.txt"
            "$<TARGET_FILE_DIR:adventure-island-ii-launcher>/SDL-LICENSE.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/gamecontrollerdb.txt"
            "$<TARGET_FILE_DIR:adventure-island-ii-launcher>/gamecontrollerdb.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/README.md"
            "$<TARGET_FILE_DIR:adventure-island-ii-launcher>/README.txt"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:adventure-island-ii-launcher>/Rom"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:adventure-island-ii-launcher>/Snapshots"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:adventure-island-ii-launcher>/Screenshots"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/Rom/Readme.txt"
            "$<TARGET_FILE_DIR:adventure-island-ii-launcher>/Rom/Readme.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/Packaging/Snapshots-README.txt"
            "$<TARGET_FILE_DIR:adventure-island-ii-launcher>/Snapshots/README.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/Packaging/Screenshots-README.txt"
            "$<TARGET_FILE_DIR:adventure-island-ii-launcher>/Screenshots/README.txt")
endif()

if(UNIX AND NOT APPLE)
    find_package(X11)
    if(X11_FOUND)
        add_executable(ai2-linux frontend/linux/ai2_linux_x11.c frontend/linux/ai2_linux_audio.c)
        target_include_directories(ai2-linux PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(ai2-linux PRIVATE ai2-frontend-common ${X11_LIBRARIES} dl m)
    endif()
endif()

if(MSVC)
    target_compile_options(ai2-static-core PRIVATE /W4)
    target_compile_options(ai2-frontend-common PRIVATE /W4)
    if(TARGET ai2-headless)
        target_compile_options(ai2-headless PRIVATE /W4)
    endif()
    if(TARGET adventure-island-ii-launcher)
        target_compile_options(adventure-island-ii-launcher PRIVATE /W4 /permissive-)
    endif()
else()
    target_compile_options(ai2-static-core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ai2-frontend-common PRIVATE -Wall -Wextra -Wpedantic)
    if(TARGET ai2-headless)
        target_compile_options(ai2-headless PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(TARGET ai2-linux)
        target_compile_options(ai2-linux PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
