#pragma once
// Opt-in diagnostic only: reads a local marker and the first two bytes of the
// current executable through the current-process handle. No scanning, no game
// entity reads, and no writes to the target process.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cwchar>

namespace reshade::chams::inprocess_probe {
struct result {
    DWORD pid = 0;
    bool is_hd_player = false;
    bool local_marker_ok = false;
    bool host_image_header_ok = false;
    DWORD marker_error = ERROR_SUCCESS;
    DWORD image_error = ERROR_SUCCESS;
};

inline result run() noexcept {
    result r;
    r.pid = GetCurrentProcessId();
    WCHAR exe[1024] = {};
    const DWORD path_len = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(sizeof(exe) / sizeof(exe[0])));
    if (path_len != 0 && path_len < sizeof(exe) / sizeof(exe[0])) {
        const WCHAR *base = std::wcsrchr(exe, L'\\');
        r.is_hd_player = _wcsicmp(base ? base + 1 : exe, L"HD-Player.exe") == 0;
    }
    const unsigned char marker[4] = {0x42, 0x75, 0x31, 0xa6};
    unsigned char copy[4] = {};
    SIZE_T count = 0;
    const BOOL marker_read = ReadProcessMemory(GetCurrentProcess(), marker, copy, sizeof(copy), &count);
    r.marker_error = marker_read ? ERROR_SUCCESS : GetLastError();
    r.local_marker_ok = marker_read && count == sizeof(copy) &&
        copy[0] == marker[0] && copy[1] == marker[1] &&
        copy[2] == marker[2] && copy[3] == marker[3];
    if (marker_read && !r.local_marker_ok) r.marker_error = ERROR_INVALID_DATA;

    HMODULE image = GetModuleHandleW(nullptr);
    if (!image) {
        r.image_error = GetLastError();
        return r;
    }
    unsigned char signature[2] = {};
    count = 0;
    const BOOL image_read = ReadProcessMemory(GetCurrentProcess(), image, signature, sizeof(signature), &count);
    r.image_error = image_read ? ERROR_SUCCESS : GetLastError();
    r.host_image_header_ok = image_read && count == sizeof(signature) &&
        signature[0] == 'M' && signature[1] == 'Z';
    if (image_read && !r.host_image_header_ok) r.image_error = ERROR_INVALID_DATA;
    return r;
}
} // namespace reshade::chams::inprocess_probe
#endif
