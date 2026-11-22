#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include "marker_protocol.hpp"

namespace controlled_marker_scan {
constexpr unsigned char kSignature[controlled_marker::kSize] = {};
constexpr std::size_t kChunk = 64 * 1024;
constexpr std::size_t kDefaultBudget = 64 * 1024 * 1024;
struct scan_options {
    const unsigned char *signature = nullptr; // exactly 64 bytes, must remain stable
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0; // exclusive
    std::size_t budget = kDefaultBudget;
    DWORD timeout_ms = 10000;
    DWORD pause_ms = 4;
    HANDLE cancel = nullptr;
    HMODULE exclude_module = nullptr;
    bool private_nocache_only = false;
};
struct scan_result {
    std::uint64_t attempted = 0, copied = 0, failures = 0, regions = 0, matches = 0;
    std::uintptr_t first_match = 0, next_cursor = 0;
    bool cancelled = false, limit_reached = false, time_reached = false, allocation_failed = false;
};
inline bool readable(const MEMORY_BASIC_INFORMATION &m) noexcept {
    if (m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
    const DWORD basic = m.Protect & 0xff;
    return basic == PAGE_READONLY || basic == PAGE_READWRITE || basic == PAGE_WRITECOPY ||
           basic == PAGE_EXECUTE_READ || basic == PAGE_EXECUTE_READWRITE || basic == PAGE_EXECUTE_WRITECOPY;
}
// Target selection is based solely on OS-reported region metadata. It does not
// establish that any selected allocation backs guest RAM.
inline bool eligible(const MEMORY_BASIC_INFORMATION &m, bool private_nocache_only) noexcept {
    if (!readable(m)) return false;
    return !private_nocache_only || (m.Type == MEM_PRIVATE && m.Protect == (PAGE_READWRITE | PAGE_NOCACHE));
}
inline scan_result scan(const scan_options &o) noexcept {
    scan_result r;
    r.next_cursor = o.begin;
    if (!o.signature || o.begin >= o.end || o.budget < sizeof(kSignature) * 2 || !o.timeout_ms) return r;
    // The dynamic marker is stored in the scan thread stack. Exclude its
    // entire allocation so we cannot "discover" our own search pattern.
    MEMORY_BASIC_INFORMATION signature_region = {};
    if (!VirtualQuery(o.signature, &signature_region, sizeof(signature_region)) || !signature_region.AllocationBase)
        return r;
    auto *buffer = static_cast<unsigned char *>(VirtualAlloc(nullptr, kChunk + sizeof(kSignature),
                                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!buffer) { r.allocation_failed = true; return r; }
    const ULONGLONG start_time = GetTickCount64();
    std::uintptr_t current = o.begin;
    std::uintptr_t resume_override = 0;
    while (current < o.end && r.attempted < o.budget) {
        if (o.cancel && WaitForSingleObject(o.cancel, 0) == WAIT_OBJECT_0) { r.cancelled = true; break; }
        if (GetTickCount64() - start_time >= o.timeout_ms) { r.time_reached = true; break; }
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(reinterpret_cast<const void *>(current), &mbi, sizeof(mbi))) break;
        ++r.regions;
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        if (mbi.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - base) break;
        const std::uintptr_t region_end = base + mbi.RegionSize;
        if (region_end <= current) break;
        const std::uintptr_t stop = (std::min)(region_end, o.end);
        if (eligible(mbi, o.private_nocache_only) && mbi.AllocationBase != o.exclude_module && mbi.AllocationBase != buffer && mbi.AllocationBase != signature_region.AllocationBase) {
            std::size_t tail = 0;
            while (current < stop && r.attempted < o.budget) {
                if (o.cancel && WaitForSingleObject(o.cancel, 0) == WAIT_OBJECT_0) { r.cancelled = true; break; }
                if (GetTickCount64() - start_time >= o.timeout_ms) { r.time_reached = true; break; }
                const std::size_t left = static_cast<std::size_t>(stop - current);
                const std::size_t request = (std::min)({kChunk, left, o.budget - static_cast<std::size_t>(r.attempted)});
                SIZE_T got = 0;
                const BOOL ok = ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(current),
                                                  buffer + tail, request, &got);
                r.attempted += request;
                if (ok && got == request) {
                    r.copied += got;
                    const std::size_t total = tail + static_cast<std::size_t>(got);
                    std::size_t at = 0;
                    while (at + sizeof(kSignature) <= total) {
                        const void *found = std::memchr(buffer + at, o.signature[0], total - at - sizeof(kSignature) + 1);
                        if (!found) break;
                        const std::size_t pos = static_cast<const unsigned char *>(found) - buffer;
                        if (std::memcmp(buffer + pos, o.signature, sizeof(kSignature)) == 0) {
                            ++r.matches;
                            if (!r.first_match) r.first_match = current - tail + pos;
                        }
                        at = pos + 1;
                    }
                    tail = (std::min)(total, sizeof(kSignature) - 1);
                    std::memmove(buffer, buffer + total - tail, tail);
                } else {
                    ++r.failures;
                    tail = 0;
                }
                current += request;
                if (o.pause_ms) {
                    if (o.cancel && WaitForSingleObject(o.cancel, o.pause_ms) == WAIT_OBJECT_0) { r.cancelled = true; break; }
                    if (!o.cancel) Sleep(o.pause_ms);
                }
            }
            // Overlap the already-read tail when resuming within the same
            // region, so a signature straddling two slices is not missed.
            if ((r.time_reached || r.attempted >= o.budget) && current < stop &&
                tail && current >= tail && current - tail > o.begin)
                resume_override = current - tail;
            if (r.cancelled || r.time_reached) break;
            if (r.attempted >= o.budget && current < o.end) { r.limit_reached = true; break; }
        }
        current = stop;
    }
    if (r.attempted >= o.budget && current < o.end) r.limit_reached = true;
    r.next_cursor = resume_override ? resume_override : current;
    VirtualFree(buffer, 0, MEM_RELEASE);
    return r;
}
} // namespace controlled_marker_scan
