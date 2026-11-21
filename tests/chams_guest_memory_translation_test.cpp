#include "../source/chams_guest_memory_translation.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using namespace reshade::chams::memory;

struct mock_host final : reader {
    std::array<unsigned char, 32> data{};
    unsigned calls = 0;
    bool partial = false;
    bool report_short_as_ok = false;
    read_result read(std::uint64_t address, void *destination, std::size_t size) noexcept override {
        ++calls;
        if (address < 0x8000 || address - 0x8000 > data.size() ||
            size > data.size() - static_cast<std::size_t>(address - 0x8000))
            return {read_status::unavailable, 0};
        const std::size_t count = partial ? size / 2 : size;
        std::memcpy(destination, data.data() + (address - 0x8000), count);
        return partial && !report_short_as_ok ? read_result{read_status::partial, count} : read_result{read_status::ok, count};
    }
};

int main() {
    mock_host host;
    for (std::size_t i = 0; i < host.data.size(); ++i)
        host.data[i] = static_cast<unsigned char>(i + 1);
    // Input segments deliberately unsorted.
    guest_memory_reader guest(host, {{0x3000, 0x8010, 8}, {0x1000, 0x8000, 8}}, 7);
    assert(guest.valid() && guest.generation() == 7);
    std::uint32_t value = 0;
    assert(read_value(guest, 0x1000, value) == read_status::ok);
    std::uint32_t expected = 0;
    std::memcpy(&expected, host.data.data(), sizeof(expected));
    assert(value == expected);
    unsigned char last = 0;
    assert(guest.read(0x3007, &last, 1).status == read_status::ok && last == 24);
    const auto calls = host.calls;
    assert(guest.read(0x1007, &value, sizeof(value)).status == read_status::unavailable);
    assert(guest.read(0x2000, &value, sizeof(value)).status == read_status::unavailable);
    assert(guest.read(0x1000, nullptr, 1).status == read_status::invalid_argument);
    assert(guest.read(0x1000, &value, 0).status == read_status::invalid_argument);
    const auto max = (std::numeric_limits<std::uint64_t>::max)();
    assert(guest.read(max, &value, 2).status == read_status::address_overflow);
    assert(host.calls == calls);

    guest_memory_reader adjacent(host, {{0x1000, 0x8000, 4}, {0x1004, 0x8004, 4}}, 1);
    assert(adjacent.valid());
    assert(adjacent.read(0x1002, &value, 4).status == read_status::unavailable);
    guest_memory_reader overlap(host, {{0x1000, 0x8000, 8}, {0x1007, 0x8010, 8}}, 1);
    assert(!overlap.valid());
    assert(overlap.read(0x1000, &value, 1).status == read_status::unavailable);
    guest_memory_reader duplicate(host, {{0x1000, 0x8000, 1}, {0x1000, 0x8010, 1}}, 1);
    assert(!duplicate.valid());
    guest_memory_reader no_generation(host, {{0x1000, 0x8000, 1}}, 0);
    assert(!no_generation.valid());
    guest_memory_reader empty(host, {}, 1);
    assert(!empty.valid());
    guest_memory_reader zero(host, {{0x1000, 0x8000, 0}}, 1);
    assert(!zero.valid());
    guest_memory_reader guest_wrap(host, {{max, 0x8000, 2}}, 1);
    assert(!guest_wrap.valid());
    guest_memory_reader host_wrap(host, {{0x1000, max, 2}}, 1);
    assert(!host_wrap.valid());
    guest_memory_reader boundary(host, {{max, max, 1}}, 1);
    assert(boundary.valid());
    std::vector<guest_host_segment> excessive(4097, {1, 1, 1});
    guest_memory_reader too_many(host, excessive, 1);
    assert(!too_many.valid());

    value = 0xdeadbeef;
    host.partial = true;
    assert(read_value(guest, 0x1000, value) == read_status::partial);
    assert(value == 0xdeadbeef); // typed reads never publish incomplete data
    host.report_short_as_ok = true;
    assert(guest.read(0x1000, &value, sizeof(value)).status == read_status::partial);
    value = 0xdeadbeef;
    assert(read_value(guest, 0x1000, value) == read_status::partial);
    assert(value == 0xdeadbeef);
    return 0;
}
