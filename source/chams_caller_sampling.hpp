#pragma once
// Bounded sampling policy for an experimental host call-site probe.
// This does not infer or store player/renderer identities.
#include <cstdint>
#include <mutex>

namespace reshade::chams::caller_probe
{
 struct selection { bool selected = false; uint64_t ordinal = 0; uint64_t sample = 0; };
 class sampler
 {
 public:
  sampler(uint64_t interval, uint64_t limit) : _interval(interval), _limit(limit) {}
  selection observe(uint64_t generation)
  {
   std::lock_guard<std::mutex> lock(_mutex);
   if (!generation || !_interval || !_limit || generation < _generation) return {};
   if (generation > _generation) { _generation = generation; _ordinal = 0; _samples = 0; }
   const uint64_t ordinal = ++_ordinal;
   if (_samples >= _limit || (ordinal - 1) % _interval) return {false, ordinal, _samples};
   return {true, ordinal, ++_samples};
  }
 private:
  std::mutex _mutex;
  uint64_t _generation = 0, _ordinal = 0, _samples = 0;
  const uint64_t _interval, _limit;
 };
}
