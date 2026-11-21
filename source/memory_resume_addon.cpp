// Separate ReShade add-on used only as a loader, no graphics hooks/events.
// The bounded scan starts only after a PID/nonce-specific trigger file appears.
#include "bounded_fingerprint_scan.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>

extern "C" {
__declspec(dllexport) const char *NAME = "Memory Fingerprint Resume Probe";
__declspec(dllexport) const char *DESCRIPTION = "Opt-in bounded read-only library fingerprint diagnostic.";
}

template <typename T> static T api_function(HMODULE module, const char *name) noexcept {
    const FARPROC proc = GetProcAddress(module, name);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(proc), "Windows function pointer size");
    std::memcpy(&result, &proc, sizeof(result));
    return result;
}
using register_fn = bool (*)(void *, std::uint32_t);
using unregister_fn = void (*)(void *);
using log_fn = void (*)(void *, int, const char *);

struct worker_context { HMODULE module = nullptr; HANDLE stop = nullptr; ULONGLONG token = 0; };
static worker_context g_context;
static HANDLE g_stop = nullptr;
static HANDLE g_thread = nullptr;

static bool make_path(WCHAR (&path)[MAX_PATH], const WCHAR *format, DWORD pid, ULONGLONG token) noexcept {
    const DWORD n = GetTempPathW(MAX_PATH, path);
    return n > 0 && n < MAX_PATH &&
        _snwprintf_s(path + n, MAX_PATH - n, _TRUNCATE, format,
                     static_cast<unsigned long>(pid), static_cast<unsigned long long>(token)) >= 0;
}
static bool write_new(const WCHAR *path, const char *text) noexcept {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const DWORD length = static_cast<DWORD>(std::strlen(text));
    DWORD written = 0;
    const BOOL ok = WriteFile(file, text, length, &written, nullptr);
    CloseHandle(file);
    return ok && written == length;
}
static DWORD WINAPI worker(LPVOID arg) noexcept {
    const auto *ctx = static_cast<const worker_context *>(arg);
    const DWORD pid = GetCurrentProcessId();
    WCHAR trigger[MAX_PATH] = {}, ready[MAX_PATH] = {}, output[MAX_PATH] = {};
    if (!make_path(trigger, L"chams-resume-trigger-%lu-%llu.flag", pid, ctx->token) ||
        !make_path(ready, L"chams-resume-ready-%lu-%llu.txt", pid, ctx->token) ||
        !make_path(output, L"chams-resume-result-%lu-%llu.txt", pid, ctx->token)) return 1;
    char status[256] = {};
    std::snprintf(status, sizeof(status), "ready pid=%lu token=%llu slice_bytes=%llu slices=8 pause_ms=2000 timeout_ms=10000\r\n",
                  static_cast<unsigned long>(pid), static_cast<unsigned long long>(ctx->token),
                  static_cast<unsigned long long>(fingerprint_probe::kDefaultBudget));
    if (!write_new(ready, status)) return 2;
    for (;;) {
        if (WaitForSingleObject(ctx->stop, 1000) != WAIT_TIMEOUT) return 0;
        const DWORD attrs = GetFileAttributesW(trigger);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) break;
    }
    HANDLE file = CreateFileW(output, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 3;
    auto append = [file](const char *text) noexcept {
        const DWORD length = static_cast<DWORD>(std::strlen(text));
        DWORD written = 0;
        return WriteFile(file, text, length, &written, nullptr) && written == length;
    };
    if (!append("[MEMORY_FINGERPRINT_RESUME] state=start max_slices=8 slice_bytes=67108864 pause_ms=2000\r\n")) {
        CloseHandle(file);
        return 4;
    }
    SYSTEM_INFO info = {};
    GetSystemInfo(&info);
    const auto end = reinterpret_cast<std::uintptr_t>(info.lpMaximumApplicationAddress);
    std::uintptr_t cursor = reinterpret_cast<std::uintptr_t>(info.lpMinimumApplicationAddress);
    const char *reason = "slice_limit";
    for (unsigned i = 0; i < 8; ++i) {
        fingerprint_probe::scan_options opts;
        opts.begin = cursor;
        opts.end = end;
        opts.cancel = ctx->stop;
        opts.exclude_module = ctx->module;
        const auto r = fingerprint_probe::scan(opts);
        char result[512] = {};
        std::snprintf(result, sizeof(result),
            "[MEMORY_FINGERPRINT_RESUME] slice=%u begin=0x%llx next=0x%llx attempted=%llu copied=%llu regions=%llu read_failures=%llu matches=%llu first_host_candidate=0x%llx limit=%d timeout=%d cancelled=%d allocation_failed=%d\r\n",
            i + 1, static_cast<unsigned long long>(cursor), static_cast<unsigned long long>(r.next_cursor),
            static_cast<unsigned long long>(r.attempted), static_cast<unsigned long long>(r.copied),
            static_cast<unsigned long long>(r.regions), static_cast<unsigned long long>(r.failures),
            static_cast<unsigned long long>(r.matches), static_cast<unsigned long long>(r.first_match),
            r.limit_reached ? 1 : 0, r.time_reached ? 1 : 0, r.cancelled ? 1 : 0, r.allocation_failed ? 1 : 0);
        if (!append(result)) { reason = "output_error"; break; }
        if (r.cancelled) { reason = "cancelled"; break; }
        if (r.allocation_failed) { reason = "allocation_failed"; break; }
        if (r.next_cursor <= cursor) { reason = "stalled"; break; }
        cursor = r.next_cursor;
        if (cursor >= end) { reason = "range_complete"; break; }
        if (i + 1 < 8 && WaitForSingleObject(ctx->stop, 2000) != WAIT_TIMEOUT) { reason = "cancelled"; break; }
    }
    char finished[128] = {};
    std::snprintf(finished, sizeof(finished), "[MEMORY_FINGERPRINT_RESUME] state=finished reason=%s\r\n", reason);
    append(finished);
    CloseHandle(file);
    return 0;
}
extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon, HMODULE reshade_module) {
    if (!addon || !reshade_module) return false;
    const auto reg = api_function<register_fn>(reshade_module, "ReShadeRegisterAddon");
    if (!reg || !reg(addon, 20)) return false;
    // ReShade calls initialization outside DllMain. No scan runs here.
    if (!g_thread) {
        g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        LARGE_INTEGER tick = {};
        QueryPerformanceCounter(&tick);
        if (g_stop) {
            g_context = {addon, g_stop, static_cast<ULONGLONG>(tick.QuadPart)};
            g_thread = CreateThread(nullptr, 0, worker, &g_context, 0, nullptr);
            if (!g_thread) { CloseHandle(g_stop); g_stop = nullptr; }
        }
    }
    const auto log = api_function<log_fn>(reshade_module, "ReShadeLogMessage");
    if (log) log(addon, g_thread ? 3 : 1, g_thread ? "[MEMORY_FINGERPRINT_RESUME] armed; no scan until trigger" : "[MEMORY_FINGERPRINT_RESUME] worker setup failed");
    return true;
}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon, HMODULE reshade_module) {
    if (g_stop) SetEvent(g_stop);
    // Wait before the loader frees this DLL; the worker checks cancellation at
    // each bounded chunk and never calls ReShade APIs from the worker.
    if (g_thread) { WaitForSingleObject(g_thread, INFINITE); CloseHandle(g_thread); g_thread = nullptr; }
    if (g_stop) { CloseHandle(g_stop); g_stop = nullptr; }
    if (!addon || !reshade_module) return;
    const auto unreg = api_function<unregister_fn>(reshade_module, "ReShadeUnregisterAddon");
    if (unreg) unreg(addon);
}
