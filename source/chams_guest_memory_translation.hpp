#pragma once
// Explicit segment-based guest-to-host adapter. No mapping discovery, no driver
// access, and no assumption that a BlueStacks guest address equals a host one.
// The caller must establish segment provenance and rebuild on target changes.
#include "chams_memory_reader.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace reshade::chams::memory {
struct guest_host_segment {
    std::uint64_t guest_base;
    std::uint64_t host_base;
    std::uint64_t length;
};

class guest_memory_reader final : public reader {
public:
    // Generation 0 is invalid. A generation identifies one observed VM/process
    // instance; callers must discard the adapter when that instance changes.
    guest_memory_reader(reader &host, std::vector<guest_host_segment> segments,
                        std::uint64_t generation)
        : host_(host), segments_(std::move(segments)), generation_(generation)
    {
        if (generation_ == 0 || segments_.empty() || segments_.size() > 4096)
            return;
        for (const auto &s : segments_) {
            if (!range_valid(s.guest_base, s.length) ||
                !range_valid(s.host_base, s.length))
                return;
        }
        std::sort(segments_.begin(), segments_.end(),
                  [](const guest_host_segment &a, const guest_host_segment &b) {
                      return a.guest_base < b.guest_base;
                  });
        for (std::size_t i = 1; i < segments_.size(); ++i) {
            const auto &prev = segments_[i - 1];
            if (segments_[i].guest_base <= prev.guest_base + (prev.length - 1))
                return; // overlapping guest mappings are ambiguous
        }
        valid_ = true;
    }

    bool valid() const noexcept { return valid_; }
    std::uint64_t generation() const noexcept { return generation_; }

    read_result read(std::uint64_t guest_address, void *destination,
                     std::size_t size) noexcept override
    {
        if (!destination || size == 0)
            return {read_status::invalid_argument, 0};
        if (!valid_range(guest_address, size))
            return {read_status::address_overflow, 0};
        if (!valid_)
            return {read_status::unavailable, 0};

        auto it = std::upper_bound(segments_.begin(), segments_.end(), guest_address,
            [](std::uint64_t a, const guest_host_segment &s) { return a < s.guest_base; });
        if (it == segments_.begin())
            return {read_status::unavailable, 0};
        --it;
        const std::uint64_t offset = guest_address - it->guest_base;
        // Require a whole request to be within one segment, even for adjacent
        // segments. This avoids silently combining incompatible mapping epochs.
        if (offset >= it->length ||
            static_cast<std::uint64_t>(size - 1) > (it->length - 1 - offset))
            return {read_status::unavailable, 0};
        const auto result = host_.read(it->host_base + offset, destination, size);
        if (result.bytes_read > size)
            return {read_status::unavailable, 0};
        if (result.status == read_status::ok && result.bytes_read != size)
            return {read_status::partial, result.bytes_read};
        return result;
    }

private:
    static bool range_valid(std::uint64_t base, std::uint64_t length) noexcept {
        return length != 0 && length - 1 <=
            (std::numeric_limits<std::uint64_t>::max)() - base;
    }
    reader &host_;
    std::vector<guest_host_segment> segments_;
    std::uint64_t generation_ = 0;
    bool valid_ = false;
};
} // namespace reshade::chams::memory
