// Separate opt-in in-process fingerprint probe. No graphics hooks or raw dumps.
#include "bounded_fingerprint_scan.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>
extern "C" {
__declspec(dllexport) const char *NAME = "Memory Fingerprint Batch Probe";
__declspec(dllexport) const char *DESCRIPTION = "Four separately triggered bounded fingerprint batches.";
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
#ifdef MEMORY_BATCH_TEST
static constexpr unsigned kSlices = 2;
static constexpr std::size_t kBudget = 65536;
static constexpr DWORD kChunkPauseMs = 50;
static constexpr DWORD kSlicePauseMs = 0;
static constexpr DWORD kPollMs = 10;
#else
static constexpr unsigned kSlices = 8;
static constexpr std::size_t kBudget = 64u * 1024u * 1024u;
static constexpr DWORD kChunkPauseMs = 4;
static constexpr DWORD kSlicePauseMs = 2000;
static constexpr DWORD kPollMs = 1000;
#endif
static constexpr unsigned kBatches = 4;
static bool make_path(WCHAR (&path)[MAX_PATH], const WCHAR *format, DWORD pid, ULONGLONG token, unsigned batch) noexcept {
    const DWORD n = GetTempPathW(MAX_PATH, path);
    return n > 0 && n < MAX_PATH &&
        _snwprintf_s(path + n, MAX_PATH - n, _TRUNCATE, format,
                     static_cast<unsigned long>(pid), static_cast<unsigned long long>(token), batch) >= 0;
}
static bool write_new(const WCHAR *path, const char *text) noexcept {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const DWORD length = static_cast<DWORD>(std::strlen(text)); DWORD written = 0;
    const BOOL ok = WriteFile(file, text, length, &written, nullptr);
    CloseHandle(file); return ok && written == length;
}
static DWORD WINAPI worker(LPVOID arg) noexcept {
    const auto *ctx = static_cast<const worker_context *>(arg);
    const DWORD pid = GetCurrentProcessId();
    SYSTEM_INFO info = {}; GetSystemInfo(&info);
    const auto end = reinterpret_cast<std::uintptr_t>(info.lpMaximumApplicationAddress);
    std::uintptr_t cursor = reinterpret_cast<std::uintptr_t>(info.lpMinimumApplicationAddress);
    for (unsigned batch = 1; batch <= kBatches; ++batch) {
        WCHAR trigger[MAX_PATH] = {}, ready[MAX_PATH] = {}, output[MAX_PATH] = {};
        if (!make_path(trigger, L"chams-batch-trigger-%lu-%llu-%u.flag", pid, ctx->token, batch) ||
            !make_path(ready, L"chams-batch-ready-%lu-%llu-%u.txt", pid, ctx->token, batch) ||
            !make_path(output, L"chams-batch-result-%lu-%llu-%u.txt", pid, ctx->token, batch)) return 1;
        char status[320] = {};
        std::snprintf(status, sizeof(status), "ready pid=%lu token=%llu batch=%u max_batches=%u begin=0x%llx slice_bytes=%llu slices=%u\r\n",
            static_cast<unsigned long>(pid), static_cast<unsigned long long>(ctx->token), batch, kBatches,
            static_cast<unsigned long long>(cursor), static_cast<unsigned long long>(kBudget), kSlices);
        if (!write_new(ready, status)) return 2;
        for (;;) {
            if (WaitForSingleObject(ctx->stop, kPollMs) != WAIT_TIMEOUT) return 0;
            const DWORD attrs = GetFileAttributesW(trigger);
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) break;
        }
        HANDLE file = CreateFileW(output, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return 3;
        auto append = [file](const char *text) noexcept {
            const DWORD length = static_cast<DWORD>(std::strlen(text)); DWORD written = 0;
            return WriteFile(file, text, length, &written, nullptr) && written == length;
        };
        char first[256] = {};
        std::snprintf(first, sizeof(first), "[MEMORY_FINGERPRINT_BATCH] state=start batch=%u begin=0x%llx max_slices=%u slice_bytes=%llu\r\n",
                      batch, static_cast<unsigned long long>(cursor), kSlices, static_cast<unsigned long long>(kBudget));
        if (!append(first)) { CloseHandle(file); return 4; }
        const char *reason = "slice_limit";
        for (unsigned i = 0; i < kSlices; ++i) {
            fingerprint_probe::scan_options opts;
            opts.begin = cursor; opts.end = end; opts.cancel = ctx->stop; opts.exclude_module = ctx->module;
            opts.budget = kBudget; opts.pause_ms = kChunkPauseMs;
            const auto r = fingerprint_probe::scan(opts);
            char line[512] = {};
            std::snprintf(line, sizeof(line),
                "[MEMORY_FINGERPRINT_BATCH] batch=%u slice=%u begin=0x%llx next=0x%llx attempted=%llu copied=%llu regions=%llu read_failures=%llu matches=%llu first_host_candidate=0x%llx limit=%d timeout=%d cancelled=%d allocation_failed=%d\r\n",
                batch, i + 1, static_cast<unsigned long long>(cursor), static_cast<unsigned long long>(r.next_cursor),
                static_cast<unsigned long long>(r.attempted), static_cast<unsigned long long>(r.copied),
                static_cast<unsigned long long>(r.regions), static_cast<unsigned long long>(r.failures),
                static_cast<unsigned long long>(r.matches), static_cast<unsigned long long>(r.first_match),
                r.limit_reached ? 1 : 0, r.time_reached ? 1 : 0, r.cancelled ? 1 : 0, r.allocation_failed ? 1 : 0);
            if (!append(line)) { reason = "output_error"; break; }
            if (r.cancelled) { reason = "cancelled"; break; }
            if (r.allocation_failed) { reason = "allocation_failed"; break; }
            if (r.matches) { cursor = r.next_cursor; reason = "match"; break; }
            if (r.next_cursor <= cursor) { reason = "stalled"; break; }
            cursor = r.next_cursor;
            if (cursor >= end) { reason = "range_complete"; break; }
            if (i + 1 < kSlices && WaitForSingleObject(ctx->stop, kSlicePauseMs) != WAIT_TIMEOUT) { reason = "cancelled"; break; }
        }
        char finished[200] = {};
        std::snprintf(finished, sizeof(finished), "[MEMORY_FINGERPRINT_BATCH] state=finished batch=%u reason=%s next=0x%llx\r\n",
                      batch, reason, static_cast<unsigned long long>(cursor));
        const bool ok = append(finished); CloseHandle(file);
        if (!ok || std::strcmp(reason, "slice_limit") != 0) return 0;
    }
    return 0;
}
extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon, HMODULE reshade_module) {
    if (!addon || !reshade_module) return false;
    const auto reg = api_function<register_fn>(reshade_module, "ReShadeRegisterAddon");
    if (!reg || !reg(addon, 20)) return false;
    if (!g_thread) {
        g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr); LARGE_INTEGER tick = {}; QueryPerformanceCounter(&tick);
        if (g_stop) { g_context = {addon, g_stop, static_cast<ULONGLONG>(tick.QuadPart)};
            g_thread = CreateThread(nullptr, 0, worker, &g_context, 0, nullptr);
            if (!g_thread) { CloseHandle(g_stop); g_stop = nullptr; }
        }
    }
    const auto log = api_function<log_fn>(reshade_module, "ReShadeLogMessage");
    if (log) log(addon, g_thread ? 3 : 1, g_thread ? "[MEMORY_FINGERPRINT_BATCH] armed; separate trigger per batch" : "[MEMORY_FINGERPRINT_BATCH] worker setup failed");
    return true;
}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon, HMODULE reshade_module) {
    if (g_stop) SetEvent(g_stop);
    if (g_thread) { WaitForSingleObject(g_thread, INFINITE); CloseHandle(g_thread); g_thread = nullptr; }
    if (g_stop) { CloseHandle(g_stop); g_stop = nullptr; }
    if (!addon || !reshade_module) return;
    const auto unreg = api_function<unregister_fn>(reshade_module, "ReShadeUnregisterAddon");
    if (unreg) unreg(addon);
}
