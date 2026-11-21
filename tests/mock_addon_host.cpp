#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <string>

static bool registered = false;
static bool unregistered = false;
extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void *, std::uint32_t version) {
    registered = version == 20; return registered;
}
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void *) { unregistered = true; }
extern "C" __declspec(dllexport) void ReShadeLogMessage(void *, int, const char *) { }

template <typename T>
static T fn(HMODULE m, const char *name) {
    FARPROC raw = GetProcAddress(m, name);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(raw), "Windows function pointer size");
    std::memcpy(&result, &raw, sizeof(result));
    return result;
}
int main() {
    WCHAR path[MAX_PATH] = {};
    const DWORD n = GetTempPathW(MAX_PATH, path);
    assert(n && n < MAX_PATH);
    assert(_snwprintf_s(path + n, MAX_PATH - n, _TRUNCATE,
        L"chams-memory-addon-selftest-%lu.txt", static_cast<unsigned long>(GetCurrentProcessId())) >= 0);
    assert(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES);
    const HMODULE addon = LoadLibraryW(L"MemoryIdentityProbe.addon64");
    assert(addon);
    const auto init = fn<bool (*)(HMODULE, HMODULE)>(addon, "AddonInit");
    const auto uninit = fn<void (*)(HMODULE, HMODULE)>(addon, "AddonUninit");
    assert(init && uninit);
    assert(init(addon, GetModuleHandleW(nullptr)) && registered);
    std::ifstream file(path);
    assert(file);
    std::string line;
    std::getline(file, line);
    assert(line.find("[MEMORY_ADDON_SELFTEST]") != std::string::npos);
    assert(line.find("host_hd_player=0") != std::string::npos);
    assert(line.find("marker_ok=1") != std::string::npos);
    assert(line.find("image_ok=1") != std::string::npos);
    file.close();
    uninit(addon, GetModuleHandleW(nullptr));
    assert(unregistered);
    assert(FreeLibrary(addon));
    assert(DeleteFileW(path));
    std::puts("PASS: standalone add-on init, self-read, output and uninit");
}
