#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

static bool registered = false;
static bool unregistered = false;
extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void *, std::uint32_t version) {
    registered = version == 20; return registered;
}
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void *) { unregistered = true; }
extern "C" __declspec(dllexport) void ReShadeLogMessage(void *, int, const char *) { }
template <typename T> static T fn(HMODULE m, const char *name) {
    FARPROC raw = GetProcAddress(m, name);
    T value = nullptr;
    static_assert(sizeof(value) == sizeof(raw), "Windows function pointer size");
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}
int main() {
    WCHAR base[MAX_PATH] = {};
    DWORD n = GetTempPathW(MAX_PATH, base);
    assert(n && n < MAX_PATH);
    WCHAR dir[MAX_PATH] = {};
    assert(_snwprintf_s(dir, MAX_PATH, _TRUNCATE, L"%sfixture-resume-%lu", base,
                        static_cast<unsigned long>(GetCurrentProcessId())) >= 0);
    assert(CreateDirectoryW(dir, nullptr));
    assert(SetEnvironmentVariableW(L"TMP", dir));
    assert(SetEnvironmentVariableW(L"TEMP", dir));
    HMODULE addon = LoadLibraryW(L"MemoryResumeProbe.addon64");
    assert(addon);
    const auto init = fn<bool (*)(HMODULE, HMODULE)>(addon, "AddonInit");
    const auto uninit = fn<void (*)(HMODULE, HMODULE)>(addon, "AddonUninit");
    assert(init && uninit && init(addon, GetModuleHandleW(nullptr)) && registered);
    std::wstring prefix = std::wstring(dir) + L"\\chams-resume-";
    std::wstring ready;
    const ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start < 5000) {
        WIN32_FIND_DATAW data = {};
        const std::wstring search = prefix + L"ready-*.txt";
        HANDLE h = FindFirstFileW(search.c_str(), &data);
        if (h != INVALID_HANDLE_VALUE) { ready = std::wstring(dir) + L"\\" + data.cFileName; FindClose(h); break; }
        Sleep(50);
    }
    assert(!ready.empty());
    const auto index = ready.find(L"ready-");
    assert(index != std::wstring::npos);
    std::wstring trigger = ready;
    trigger.replace(index, 6, L"trigger-");
    trigger.replace(trigger.size() - 4, 4, L".flag");
    std::wstring output = ready;
    output.replace(index, 6, L"result-");
    HANDLE flag = CreateFileW(trigger.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(flag != INVALID_HANDLE_VALUE); CloseHandle(flag);
    const ULONGLONG scan_start = GetTickCount64();
    while (GetTickCount64() - scan_start < 20000 && GetFileAttributesW(output.c_str()) == INVALID_FILE_ATTRIBUTES) Sleep(50);
    // The file is created before the worker writes the header. Unload first:
    // this cancels any ongoing slice and guarantees the report handle is closed.
    uninit(addon, GetModuleHandleW(nullptr));
    assert(unregistered && FreeLibrary(addon));
    std::ifstream file(output.c_str());
    assert(file);
    std::string line; std::getline(file, line); file.close();
    assert(line.find("[MEMORY_FINGERPRINT_RESUME]") != std::string::npos);
    assert(line.find("state=start") != std::string::npos);
    assert(DeleteFileW(trigger.c_str()));
    assert(DeleteFileW(output.c_str()));
    assert(DeleteFileW(ready.c_str()));
    assert(RemoveDirectoryW(dir));
    std::puts("PASS: add-on registration, trigger, bounded scan, report and orderly unload");
}
