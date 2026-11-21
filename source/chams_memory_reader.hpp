#pragma once
// Read-only foundation. Addresses are in the backend's address space; no
// guest/host translation, IL2CPP layouts, or draw correlation is implied.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace reshade::chams::memory {
enum class read_status { ok, invalid_argument, address_overflow, access_denied, partial, unavailable };
struct read_result { read_status status = read_status::unavailable; std::size_t bytes_read = 0; };

class reader {
public:
 virtual ~reader() = default;
 // Implementations never write to the target. A partial read is not success.
 virtual read_result read(std::uint64_t address, void *destination, std::size_t size) noexcept = 0;
};

inline bool valid_range(std::uint64_t address, std::size_t size) noexcept {
 return size != 0 && static_cast<std::uint64_t>(size - 1) <= (std::numeric_limits<std::uint64_t>::max)() - address;
}

template<class T>
read_status read_value(reader &source, std::uint64_t address, T &out) noexcept {
 static_assert(std::is_trivially_copyable<T>::value, "only trivially copyable types");
 // Only publish a complete value; never expose partially read data.
 T temporary{};
 if (!valid_range(address, sizeof(T))) return read_status::address_overflow;
 const auto result = source.read(address, &temporary, sizeof(T));
 if (result.status != read_status::ok) return result.status;
 if (result.bytes_read != sizeof(T)) return read_status::partial;
 std::memcpy(&out, &temporary, sizeof(T));
 return read_status::ok;
}

// Identity must stay unknown until a validated, version-specific backend AND
// entity-to-draw correlation exist. A process read by itself proves neither.
enum class ownership { unknown, local, teammate, enemy };
} // namespace reshade::chams::memory
