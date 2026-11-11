#include "chams_capture.hpp"
#include <cstdio>
#include <limits>
#include <sstream>
#include <memory>
#include <locale>
#include <shared_mutex>

namespace reshade::chams::capture
{
 namespace { std::atomic<uint64_t> next_thread_id{0}, metadata_epoch{1}; }
 uint64_t metadata_generation() { return metadata_epoch.load(std::memory_order_acquire); }
 void invalidate_metadata() { metadata_epoch.fetch_add(1, std::memory_order_acq_rel); }
 uint64_t recorder::now_ns() { return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
 uint64_t recorder::thread_id() { static thread_local const uint64_t id = ++next_thread_id; return id; }
 recorder::~recorder() { stop(); }

 bool recorder::start(const std::filesystem::path &directory, size_t queue_limit, uint64_t rotate_bytes)
 {
  std::lock_guard<std::mutex> lifecycle(_lifecycle);
  std::lock_guard<std::mutex> lock(_mutex);
  if (_enabled || _worker.joinable() || queue_limit == 0 || rotate_bytes < 256) return false;
  std::error_code ec;
  if (!std::filesystem::create_directory(directory, ec) || ec) return false; // never overwrite an existing session
  _directory = directory; _queue_limit = queue_limit; _rotate_bytes = rotate_bytes;
  _queue.clear(); _stopping = false; _io_error = false; _file_bytes = 0; _writer_files = 0;
  _start_ns = now_ns(); _end_ns = 0; _next_seq = 0;
  const auto unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
  _attempted = _queued = _written = _dropped = 0;
  _frames = _contexts = _draws = _files = _bytes = _enqueue_ns = 0;
  _queue_full = _oversized = _io_dropped = _allocation_failed = _high_water = _partial_frames = 0;
  _instrumentation_ns.store(0); _instrumentation_samples.store(0);
  if (!open_next()) { _io_error = true; return false; }
  _files = _writer_files;
  try {
   _queue.push_back({"{\"v\":1,\"seq\":1,\"t_ns\":0,\"thread\":" + std::to_string(thread_id()) +
    ",\"context\":0,\"frame\":0,\"event\":\"session_start\",\"unix_ms\":" + std::to_string(unix_ms) + "}\n"});
   _next_seq = _attempted = _queued = _high_water = 1;
  } catch (...) { _file.close(); return false; }
  _enabled.store(true, std::memory_order_release);
  try { _worker = std::thread(&recorder::writer, this); }
  catch (...) { _enabled.store(false, std::memory_order_release); _io_error = true; _file.close(); return false; }
  return true;
 }

 void recorder::stop()
 {
  std::lock_guard<std::mutex> lifecycle(_lifecycle);
  {
   std::lock_guard<std::mutex> lock(_mutex);
   if (!_worker.joinable()) return;
   _enabled.store(false, std::memory_order_release);
   _stopping = true; _end_ns = now_ns();
   // A single control marker may exceed the queue capacity; never block the
   // stopping thread waiting for the writer to consume a full queue.
   try {
    const uint64_t seq = ++_next_seq; ++_attempted;
    _queue.push_back({"{\"v\":1,\"seq\":" + std::to_string(seq) +
     ",\"t_ns\":" + std::to_string(_end_ns - _start_ns) +
     ",\"thread\":" + std::to_string(thread_id()) +
     ",\"context\":0,\"frame\":0,\"event\":\"session_end\"}\n"});
    ++_queued;
    if (_queue.size() > _high_water) _high_water = _queue.size();
   } catch (...) { ++_dropped; ++_allocation_failed; }
  }
  _ready.notify_one();
  _worker.join();
 }
 void recorder::instrumentation_time(uint64_t ns) { _instrumentation_ns.fetch_add(ns, std::memory_order_relaxed); _instrumentation_samples.fetch_add(1, std::memory_order_relaxed); }
 void recorder::external_drop()
 {
  std::lock_guard<std::mutex> lock(_mutex);
  if (_enabled.load()) { ++_attempted; ++_next_seq; ++_dropped; ++_allocation_failed; }
 }
 std::filesystem::path recorder::directory() const { std::lock_guard<std::mutex> lock(_mutex); return _directory; }
 status recorder::snapshot() const
 {
  std::lock_guard<std::mutex> lock(_mutex);
  return {_enabled.load(), _io_error, _attempted, _queued, _written, _dropped,
          _frames, _contexts, _draws, _files, _bytes, _enqueue_ns,
          _start_ns == 0 ? 0 : (_end_ns ? _end_ns : now_ns()) - _start_ns,
          _instrumentation_ns.load(std::memory_order_relaxed), _instrumentation_samples.load(std::memory_order_relaxed),
          _queue_full, _oversized, _io_dropped, _allocation_failed, _high_water, _partial_frames};
 }
 bool recorder::emit(const char *kind, uint64_t context, uint64_t frame, const std::string &body)
 {
  if (!_enabled.load(std::memory_order_acquire)) return false;
  const uint64_t begin = now_ns();
  std::unique_lock<std::mutex> lock(_mutex);
  if (!_enabled.load(std::memory_order_relaxed)) return false;
  ++_attempted;
  const uint64_t seq = ++_next_seq;
  if (_io_error || _queue.size() >= _queue_limit || body.size() > 8192)
  {
   ++_dropped;
   if (_io_error) ++_io_dropped;
   else if (body.size() > 8192) ++_oversized;
   else ++_queue_full;
   _enqueue_ns += now_ns() - begin; return false;
  }
  try {
   // kind and body are internal, fixed-key JSON generated by the instrumentation.
   std::string s = "{\"v\":1,\"seq\":" + std::to_string(seq) +
    ",\"t_ns\":" + std::to_string(begin - _start_ns) +
    ",\"thread\":" + std::to_string(thread_id()) +
    ",\"context\":" + std::to_string(context) +
    ",\"frame\":" + std::to_string(frame) + ",\"event\":\"" + kind + "\"";
   if (!body.empty()) s += "," + body;
   s += "}\n";
   _queue.push_back({std::move(s)}); ++_queued;
   if (_queue.size() > _high_water) _high_water = _queue.size();
   if (std::string(kind) == "draw") ++_draws;
   else if (std::string(kind) == "frame_begin") ++_frames;
   else if (std::string(kind) == "context_create" || std::string(kind) == "context_observed") ++_contexts;
   else if (std::string(kind) == "frame_end" && (body.find("session_stop_incomplete") != std::string::npos || body.find("context_destroyed") != std::string::npos)) ++_partial_frames;
  } catch (...) { ++_dropped; ++_allocation_failed; _enqueue_ns += now_ns() - begin; return false; }
  _enqueue_ns += now_ns() - begin;
  lock.unlock(); _ready.notify_one(); return true;
 }
 bool recorder::open_next()
 {
  char name[48]; std::snprintf(name, sizeof(name), "capture-%06llu.jsonl", static_cast<unsigned long long>(_writer_files + 1));
  _file.open(_directory / name, std::ios::binary | std::ios::out | std::ios::trunc);
  if (!_file) return false;
  ++_writer_files; _file_bytes = 0; return true;
 }
 bool recorder::write_line(const std::string &s)
 {
  if (_file_bytes && _file_bytes + s.size() > _rotate_bytes)
  {
   _file.flush(); if (!_file) return false;
   _file.close(); if (!open_next()) return false;
  }
  _file.write(s.data(), static_cast<std::streamsize>(s.size()));
  if (!_file) return false;
  _file_bytes += s.size(); return true;
 }
 void recorder::finish_summary()
 {
  const status s = snapshot();
  // Summary is a separate file and is not included in the sequenced event stream.
  std::ofstream f(_directory / "summary.json", std::ios::binary | std::ios::trunc);
  if (f) {
   f.imbue(std::locale::classic());
   f << "{\"v\":1,\"complete\":" << (s.io_error || s.dropped || s.partial_frames ? "false" : "true")
     << ",\"io_error\":" << (s.io_error ? "true" : "false")
     << ",\"queue_capacity_records\":" << _queue_limit << ",\"rotation_target_bytes\":" << _rotate_bytes
     << ",\"attempted\":" << s.attempted << ",\"queued\":" << s.queued
     << ",\"written\":" << s.written << ",\"dropped\":" << s.dropped
     << ",\"frames\":" << s.frames << ",\"contexts\":" << s.contexts
     << ",\"draws\":" << s.draws << ",\"files\":" << s.files << ",\"bytes\":" << s.bytes
     << ",\"elapsed_ns\":" << s.elapsed_ns << ",\"enqueue_ns\":" << s.enqueue_ns
     << ",\"instrumentation_ns\":" << s.instrumentation_ns << ",\"instrumentation_samples\":" << s.instrumentation_samples
     << ",\"queue_full\":" << s.queue_full << ",\"oversized\":" << s.oversized
     << ",\"io_dropped\":" << s.io_dropped << ",\"allocation_failed\":" << s.allocation_failed
     << ",\"high_water\":" << s.high_water << ",\"partial_frames\":" << s.partial_frames << "}\n";
   f.flush();
  }
  if (!f) { std::lock_guard<std::mutex> lock(_mutex); _io_error = true; }
 }
 void recorder::writer()
 {
  uint64_t last_flush = now_ns();
  for (;;) {
   std::unique_lock<std::mutex> lock(_mutex);
   _ready.wait_for(lock, std::chrono::seconds(1), [&] { return _stopping || !_queue.empty(); });
   if (_queue.empty() && _stopping) break;
   if (_queue.empty()) {
    const bool can_flush = !_io_error; lock.unlock();
    if (can_flush) { _file.flush(); if (!_file) { lock.lock(); _io_error = true; lock.unlock(); } }
    last_flush = now_ns(); continue;
   }
   line item = std::move(_queue.front()); _queue.pop_front();
   const bool can_write = !_io_error;
   lock.unlock();
   bool ok = can_write && write_line(item.value); // no producer mutex held during I/O
   if (ok && now_ns() - last_flush >= 1000000000ull) {
    _file.flush(); ok = static_cast<bool>(_file); last_flush = now_ns();
   }
   lock.lock();
   _files = _writer_files;
   if (ok) { ++_written; _bytes += item.value.size(); }
   else { _io_error = true; ++_dropped; ++_io_dropped; }
  }
  _file.flush(); const bool flush_ok = static_cast<bool>(_file); _file.close();
  { std::lock_guard<std::mutex> lock(_mutex); if (!flush_ok) _io_error = true; }
  finish_summary();
 }

 correlator::context &correlator::ensure(uintptr_t native)
 {
  auto it = _contexts.find(native);
  if (it != _contexts.end()) return it->second;
  context c; c.id = ++_next_context;
  auto pair = _contexts.emplace(native, c);
  _recorder.emit("context_observed", c.id, 0);
  return pair.first->second;
 }
 uint64_t correlator::created(uintptr_t native)
 {
  if (!native) return 0;
  std::lock_guard<std::mutex> lock(_mutex);
  auto it = _contexts.find(native);
  if (it != _contexts.end()) return it->second.id;
  context c; c.id = ++_next_context; _contexts.emplace(native, c);
  _recorder.emit("context_create", c.id, 0); return c.id;
 }
 void correlator::destroyed(uintptr_t native)
 {
  if (!native) return;
  std::lock_guard<std::mutex> lock(_mutex);
  auto it = _contexts.find(native);
  if (it == _contexts.end()) return;
  for (const auto &entry : it->second.streams) if (entry.second.begun)
   _recorder.emit("frame_end", it->second.id, entry.second.frame, "\"surface\":" + std::to_string(entry.first) + ",\"reason\":\"context_destroyed\"");
  _recorder.emit("context_destroy", it->second.id, 0);
  _contexts.erase(it);
 }
 void correlator::switched(uintptr_t native)
 {
  std::lock_guard<std::mutex> lock(_mutex);
  if (!native) { _recorder.emit("context_switch", 0, 0); return; }
  auto &c = ensure(native); _recorder.emit("context_switch", c.id, 0);
 }
 void correlator::shared(uintptr_t first, uintptr_t second, bool succeeded)
 {
  std::lock_guard<std::mutex> lock(_mutex);
  const uint64_t a = first ? ensure(first).id : 0;
  const uint64_t b = second ? ensure(second).id : 0;
  _recorder.emit("context_share", a, 0, "\"peer_context\":" + std::to_string(b) + (succeeded ? ",\"success\":true" : ",\"success\":false"));
 }
 uint64_t correlator::surface_locked(uintptr_t native)
 {
  if (!native) return 0;
  auto it = _surfaces.find(native); if (it != _surfaces.end()) return it->second;
  const auto id = ++_next_surface; _surfaces.emplace(native, id);
  _recorder.emit("surface_observed", 0, 0, "\"surface\":" + std::to_string(id)); return id;
 }
 uint64_t correlator::surface(uintptr_t native) { std::lock_guard<std::mutex> lock(_mutex); return surface_locked(native); }
 void correlator::begin(context &c, uint64_t surface)
 {
  auto &stream = c.streams[surface];
  if (!stream.begun) { _recorder.emit("frame_begin", c.id, stream.frame, "\"surface\":" + std::to_string(surface) + (stream.frame == 1 ? ",\"begin_reason\":\"first_observed_unknown\"" : ",\"begin_reason\":\"after_present\"")); stream.begun = true; }
 }
 void correlator::draw(uintptr_t native, const std::string &body, uint64_t surface)
 {
  if (!native) { _recorder.emit("draw", 0, 0, body); return; }
  std::lock_guard<std::mutex> lock(_mutex);
  auto &c = ensure(native); begin(c, surface);
  _recorder.emit("draw", c.id, c.streams[surface].frame, "\"surface\":" + std::to_string(surface) + (body.empty() ? "" : "," + body));
 }
 draw_ticket correlator::begin_draw(uintptr_t native, uint64_t surface)
 {
  std::lock_guard<std::mutex> lock(_mutex);
  draw_ticket t; t.id = ++_next_draw; t.surface = surface;
  if (native) { auto &c = ensure(native); begin(c, surface); t.context = c.id; t.frame = c.streams[surface].frame; }
  _recorder.emit("draw_begin", t.context, t.frame, "\"surface\":" + std::to_string(t.surface) + ",\"draw_id\":" + std::to_string(t.id));
  return t;
 }
 void correlator::end_draw(const draw_ticket &ticket, const std::string &body)
 {
  _recorder.emit("draw", ticket.context, ticket.frame,
   "\"surface\":" + std::to_string(ticket.surface) + ",\"draw_id\":" + std::to_string(ticket.id) + (body.empty() ? "" : "," + body));
 }
 void correlator::event(uintptr_t native, const char *kind, const std::string &body, uint64_t surface)
 {
  if (!native) { _recorder.emit(kind, 0, 0, body); return; }
  std::lock_guard<std::mutex> lock(_mutex);
  auto &c = ensure(native);
  if (surface == uint64_t(-1)) _recorder.emit(kind, c.id, 0, body);
  else { begin(c, surface); _recorder.emit(kind, c.id, c.streams[surface].frame, "\"surface\":" + std::to_string(surface) + (body.empty() ? "" : "," + body)); }
 }
 void correlator::present_begin(uintptr_t native, uintptr_t native_surface)
 {
  if (!native) return;
  std::lock_guard<std::mutex> lock(_mutex);
  auto &c = ensure(native); const auto sid = surface_locked(native_surface); begin(c, sid);
  _recorder.emit("present_begin", c.id, c.streams[sid].frame, "\"surface\":" + std::to_string(sid));
 }
 void correlator::present(uintptr_t native, bool succeeded, uintptr_t native_surface)
 {
  if (!native) return;
  std::lock_guard<std::mutex> lock(_mutex);
  auto &c = ensure(native); const auto sid = surface_locked(native_surface); begin(c, sid); auto &stream = c.streams[sid];
  _recorder.emit("present", c.id, stream.frame, "\"surface\":" + std::to_string(sid) + (succeeded ? ",\"success\":true" : ",\"success\":false"));
  if (succeeded) { _recorder.emit("frame_end", c.id, stream.frame, "\"surface\":" + std::to_string(sid) + ",\"reason\":\"present\""); ++stream.frame; stream.begun = false; }
 }
 void correlator::finish()
 {
  std::lock_guard<std::mutex> lock(_mutex);
  for (const auto &entry : _contexts) {
   const auto &c = entry.second;
   for (const auto &stream : c.streams) if (stream.second.begun)
    _recorder.emit("frame_end", c.id, stream.second.frame, "\"surface\":" + std::to_string(stream.first) + ",\"reason\":\"session_stop_incomplete\"");
  }
  _contexts.clear();
 }

 namespace {
  struct runtime_controller {
   std::mutex lifecycle;
   std::shared_mutex gate;
   std::mutex flight_mutex;
   std::condition_variable flight_ready;
   uint64_t inflight = 0;
   std::atomic<bool> active{false};
   recorder output;
   std::unique_ptr<correlator> correlation;
  };
  // The host may unload from DllMain under the Windows loader lock; joining a
  // worker there can deadlock. Explicit UI stop drains the process-lifetime
  // controller. Abrupt termination can leave an incomplete final part.
  runtime_controller &controller() { static auto *c = new runtime_controller; return *c; }
 }
 bool runtime_enabled() { return controller().active.load(std::memory_order_acquire); }
 bool runtime_start(const std::filesystem::path &parent)
 {
  auto &c = controller(); std::lock_guard<std::mutex> life(c.lifecycle);
  std::unique_lock<std::shared_mutex> lock(c.gate);
  if (c.active.load()) return false;
  std::error_code ec; std::filesystem::create_directories(parent, ec); if (ec) return false;
  static std::atomic<uint64_t> counter{0};
  const auto epoch = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  bool ok = false;
  for (int i = 0; i < 16 && !ok; ++i) {
   auto name = std::string("session-") + std::to_string(epoch) + "-" + std::to_string(++counter);
   ok = c.output.start(parent / name, 32768, 64ull*1024*1024);
  }
  if (!ok) return false;
  try { c.correlation = std::make_unique<correlator>(c.output); }
  catch (...) { lock.unlock(); c.output.stop(); return false; }
  invalidate_metadata(); // Re-emit program metadata in every new session.
  c.active.store(true, std::memory_order_release); return true;
 }
 void runtime_stop()
 {
  auto &c = controller(); std::lock_guard<std::mutex> life(c.lifecycle);
  { std::unique_lock<std::shared_mutex> lock(c.gate);
   if (!c.active.exchange(false)) return;
  }
  { std::unique_lock<std::mutex> lock(c.flight_mutex);
   c.flight_ready.wait(lock, [&] { return c.inflight == 0; });
  }
  { std::unique_lock<std::shared_mutex> lock(c.gate);
   try { c.correlation->finish(); } catch (...) { c.output.external_drop(); }
   c.correlation.reset();
  }
  c.output.stop();
 }
 bool runtime_begin_draw()
 {
  auto &c = controller(); if (!c.active.load(std::memory_order_acquire)) return false;
  std::shared_lock<std::shared_mutex> lock(c.gate);
  if (!c.active.load(std::memory_order_relaxed)) return false;
  std::lock_guard<std::mutex> flight(c.flight_mutex); ++c.inflight; return true;
 }
 draw_ticket runtime_bind_draw(uintptr_t n, uint64_t surface) noexcept
 {
  auto &c = controller();
  try { std::shared_lock<std::shared_mutex> lock(c.gate); if (c.correlation) return c.correlation->begin_draw(n, surface); }
  catch (...) { c.output.external_drop(); }
  return {};
 }
 void runtime_end_draw(const draw_ticket &ticket, const std::string *body) noexcept
 {
  auto &c = controller();
  try {
   std::shared_lock<std::shared_mutex> lock(c.gate);
   if (!body || !c.correlation || !ticket.id) c.output.external_drop();
   else c.correlation->end_draw(ticket, *body);
  } catch (...) { c.output.external_drop(); }
  { std::lock_guard<std::mutex> lock(c.flight_mutex); if (c.inflight) --c.inflight; }
  c.flight_ready.notify_all();
 }
 uint64_t runtime_inflight_surface(uintptr_t native) noexcept
 {
  auto &c = controller();
  try { std::shared_lock<std::shared_mutex> lock(c.gate); return c.correlation ? c.correlation->surface(native) : 0; }
  catch (...) { c.output.external_drop(); return 0; }
 }
 void runtime_instrumentation_time(uint64_t ns) noexcept { controller().output.instrumentation_time(ns); }
 void runtime_record_loss() noexcept { try { controller().output.external_drop(); } catch (...) {} }
 void runtime_inflight_event(uintptr_t n, const char *kind, const std::string &body, uint64_t surface) noexcept
 {
  auto &c = controller();
  try { std::shared_lock<std::shared_mutex> lock(c.gate);
   if (c.correlation) c.correlation->event(n, kind, body, surface); else c.output.external_drop();
  } catch (...) { c.output.external_drop(); }
 }
 status runtime_status() { return controller().output.snapshot(); }
 std::filesystem::path runtime_directory() { return controller().output.directory(); }
 template <typename F> void runtime_event(F &&f)
 {
  auto &c = controller(); if (!c.active.load(std::memory_order_acquire)) return;
  try { std::shared_lock<std::shared_mutex> lock(c.gate);
   if (c.active.load(std::memory_order_relaxed) && c.correlation) f(*c.correlation);
  } catch (...) { c.output.external_drop(); }
 }
 void runtime_created(uintptr_t n) { runtime_event([&](correlator &c){ c.created(n); }); }
 void runtime_destroyed(uintptr_t n) { runtime_event([&](correlator &c){ c.destroyed(n); }); }
 void runtime_switched(uintptr_t n) { runtime_event([&](correlator &c){ c.switched(n); }); }
 void runtime_shared(uintptr_t a, uintptr_t b, bool ok) { runtime_event([&](correlator &c){ c.shared(a, b, ok); }); }
 void runtime_marker(uintptr_t n, uintptr_t surface, const char *source)
 {
  runtime_event([&](correlator &c){ const auto sid = c.surface(surface);
   c.event(n, "marker", source && std::string(source) == "f9" ? "\"source\":\"f9\"" : "\"source\":\"menu\"", sid); });
 }
 void runtime_draw(uintptr_t n, const std::string &body) { runtime_event([&](correlator &c){ c.draw(n, body); }); }
 void runtime_event(uintptr_t n, const char *kind, const std::string &body) { runtime_event([&](correlator &c){ c.event(n, kind, body); }); }
 void runtime_present_begin(uintptr_t n, uintptr_t surface) { runtime_event([&](correlator &c){ c.present_begin(n, surface); }); }
 void runtime_present(uintptr_t n, uintptr_t surface, bool ok) { runtime_event([&](correlator &c){ c.present(n, ok, surface); }); }

}
