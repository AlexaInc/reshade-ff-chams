#include "../source/bounded_fingerprint_scan.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace fingerprint_probe;
int main() {
    constexpr std::size_t size = kChunk * 2;
    auto *p = static_cast<unsigned char *>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(p);
    std::memset(p, 0, size);
    std::memcpy(p + kChunk - 20, kSignature, sizeof(kSignature)); // crosses chunk boundary
    std::memcpy(p + kChunk + 500, kSignature, sizeof(kSignature));
    scan_options o;
    o.begin = reinterpret_cast<std::uintptr_t>(p);
    o.end = o.begin + size;
    o.budget = size;
    o.pause_ms = 0;
    const auto full = scan(o);
    assert(full.attempted == size && full.copied == size && full.matches == 2);
    assert(full.first_match == o.begin + kChunk - 20);
    o.budget = kChunk;
    const auto bounded = scan(o);
    assert(bounded.attempted == kChunk && bounded.matches == 0 && bounded.limit_reached);
    HANDLE cancel = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    assert(cancel);
    o.cancel = cancel;
    const auto cancelled = scan(o);
    assert(cancelled.cancelled && cancelled.attempted == 0);
    CloseHandle(cancel);
    DWORD old_protect = 0;
    assert(VirtualProtect(p, size, PAGE_NOACCESS, &old_protect));
    o.cancel = nullptr; o.budget = size;
    const auto blocked = scan(o);
    assert(blocked.attempted == 0 && blocked.matches == 0);
    assert(VirtualFree(p, 0, MEM_RELEASE));
    std::puts("PASS: bounded scan, cross-chunk match, cancellation, and inaccessible pages");
}
