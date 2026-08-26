#include "ai2_rom.h"

#include "ai2_sha256.h"

#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
constexpr size_t kExpectedBytes = 262160u;
constexpr std::array<uint8_t, 32> kExpectedSha = {
    0x10,0xdd,0x53,0xfd,0x62,0xc9,0x5e,0x77,0x85,0x6b,0x09,0xc6,0x93,0xb3,0xbd,0xd1,
    0x4b,0x62,0xa8,0x99,0x88,0x90,0x2e,0xea,0x11,0x12,0x57,0xca,0xfe,0x9b,0x8a,0xcc};

void set_error(char *out, size_t capacity, const char *message) {
    if (!out || capacity == 0u) return;
    std::snprintf(out, capacity, "%s", message ? message : "Unknown ROM error");
}

FILE *open_utf8(const char *path, const wchar_t *mode) {
    if (!path || !*path) return nullptr;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
    if (count <= 0) return nullptr;
    std::wstring wide(static_cast<size_t>(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide.data(), count)) return nullptr;
    FILE *file = nullptr;
    return _wfopen_s(&file, wide.c_str(), mode) == 0 ? file : nullptr;
}
}

extern "C" int ai2_rom_load(const char *path, AI2Rom *rom, char *error, size_t error_capacity) {
    if (!path || !rom) { set_error(error, error_capacity, "Invalid ROM path or output"); return 0; }
    std::memset(rom, 0, sizeof(*rom));
    FILE *file = nullptr;
    file = open_utf8(path, L"rb");
    if (!file) { set_error(error, error_capacity, "Cannot open ROM"); return 0; }
    if (std::fseek(file, 0, SEEK_END) != 0) { std::fclose(file); set_error(error, error_capacity, "Cannot size ROM"); return 0; }
    const long length = std::ftell(file);
    if (length < 0 || std::fseek(file, 0, SEEK_SET) != 0) { std::fclose(file); set_error(error, error_capacity, "Cannot size ROM"); return 0; }
    rom->file_data = static_cast<uint8_t *>(std::malloc(static_cast<size_t>(length)));
    if (!rom->file_data) { std::fclose(file); set_error(error, error_capacity, "Out of memory"); return 0; }
    rom->file_size = static_cast<size_t>(length);
    if (std::fread(rom->file_data, 1, rom->file_size, file) != rom->file_size) {
        std::fclose(file); ai2_rom_free(rom); set_error(error, error_capacity, "Short ROM read"); return 0;
    }
    std::fclose(file); set_error(error, error_capacity, ""); return 1;
}

extern "C" void ai2_rom_free(AI2Rom *rom) {
    if (!rom) return;
    std::free(rom->file_data);
    std::memset(rom, 0, sizeof(*rom));
}

extern "C" int ai2_rom_is_expected(const AI2Rom *rom, char *reason, size_t reason_capacity) {
    if (!rom || !rom->file_data) { set_error(reason, reason_capacity, "ROM is not loaded"); return 0; }
    if (rom->file_size != kExpectedBytes) { set_error(reason, reason_capacity, "File size does not match Adventure Island II (USA)"); return 0; }
    static constexpr uint8_t header[16] = {'N','E','S',0x1a,8,16,0x40,0,0,0,0,0,0,0,0,0};
    if (std::memcmp(rom->file_data, header, sizeof(header)) != 0) { set_error(reason, reason_capacity, "iNES header does not match Adventure Island II (USA)"); return 0; }
    std::array<uint8_t, 32> digest{};
    ai2_sha256(rom->file_data, rom->file_size, digest.data());
    if (digest != kExpectedSha) { set_error(reason, reason_capacity, "SHA-256 does not match Adventure Island II (USA)"); return 0; }
    set_error(reason, reason_capacity, "Exact Adventure Island II (USA) ROM"); return 1;
}
