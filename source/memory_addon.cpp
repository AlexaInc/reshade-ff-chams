// Standalone ReShade-loaded diagnostic module. No graphics event registration,
// no graphics hooks, no other-process access, and no entity memory scanning.
#include "memory_selftest.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>

extern "C" {
__declspec(dllexport) const char *NAME = "Memory Identity Probe";
__declspec(dllexport) const char *DESCRIPTION = "Opt-in self-read feasibility diagnostic; no player detection.";
}

template <typename T>
static T api_function(HMODULE module, const char *name) noexcept {
    const FARPROC proc = GetProcAddress(module, name);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(proc), "Windows function pointer size");
    std::memcpy(&result, &proc, sizeof(result));
    return result;
}

using register_addon_fn = bool (*)(void *, std::uint32_t);
using unregister_addon_fn = void (*)(void *);
using log_message_fn = void (*)(void *, int, const char *);
constexpr std::uint32_t kReshadeApiVersion = 20;

extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon, HMODULE reshade_module) {
    if (!addon || !reshade_module) return false;
    const auto register_addon = api_function<register_addon_fn>(reshade_module, "ReShadeRegisterAddon");
    if (!register_addon || !register_addon(addon, kReshadeApiVersion)) return false;

    const auto r = reshade::chams::inprocess_probe::run();
    char line[256] = {};
    std::snprintf(line, sizeof(line),
        "[MEMORY_ADDON_SELFTEST] pid=%lu host_hd_player=%d marker_ok=%d marker_error=%lu image_ok=%d image_error=%lu\r\n",
        static_cast<unsigned long>(r.pid), r.is_hd_player ? 1 : 0,
        r.local_marker_ok ? 1 : 0, static_cast<unsigned long>(r.marker_error),
        r.host_image_header_ok ? 1 : 0, static_cast<unsigned long>(r.image_error));

    // A unique result file avoids overwriting captures or existing log files.
    WCHAR path[MAX_PATH] = {};
    const DWORD len = GetTempPathW(MAX_PATH, path);
    if (len > 0 && len < MAX_PATH &&
        _snwprintf_s(path + len, MAX_PATH - len, _TRUNCATE,
                     L"chams-memory-addon-selftest-%lu.txt", static_cast<unsigned long>(r.pid)) >= 0) {
        HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(file, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
            CloseHandle(file);
        }
    }
    const auto log = api_function<log_message_fn>(reshade_module, "ReShadeLogMessage");
    if (log) log(addon, 3, line);
    return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon, HMODULE reshade_module) {
    if (!addon || !reshade_module) return;
    const auto unregister_addon = api_function<unregister_addon_fn>(reshade_module, "ReShadeUnregisterAddon");
    if (unregister_addon) unregister_addon(addon);
}
