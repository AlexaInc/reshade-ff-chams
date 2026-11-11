#include "../source/chams_capture.hpp"
#include <cassert>
#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
using namespace reshade::chams::capture;
int main(int argc, char **argv)
{
 assert(argc == 2);
 const std::filesystem::path root(argv[1]);
 recorder r;
 assert(r.start(root / "long", 500000, 262144));
 assert(!r.start(root / "again"));
 correlator c(r);
 const auto first = c.created(10), second = c.created(20);
 assert(first && second && first != second);
 c.shared(10, 20, true); c.shared(10, 20, false);
 c.switched(10); c.draw(10, "\"value\":1"); c.present(10, false);
 c.draw(20, "\"value\":2"); c.present(20, true);
 c.draw(10, "\"value\":3"); c.present(10, true); c.destroyed(10);
 const auto reused = c.created(10); assert(reused != first && reused != second);
 c.draw(10, "\"value\":4"); c.present(10, true);
 std::vector<std::thread> frame_workers;
 for (int t = 0; t < 4; ++t) { c.created(30 + t); }
 for (int t = 0; t < 4; ++t) frame_workers.emplace_back([&, t] {
  for (int i = 0; i < 1000; ++i) {
   c.draw(30 + t, "\"round\":" + std::to_string(i));
   c.draw(30 + t, "\"round\":" + std::to_string(i));
   c.present(30 + t, true);
  }
 });
 for (auto &t : frame_workers) t.join();
 std::vector<std::thread> workers;
 for (int t = 0; t < 4; ++t) workers.emplace_back([&, t] {
  for (int i = 0; i < 100000; ++i) {
   const std::string body = "\"worker\":" + std::to_string(t) + ",\"index\":" + std::to_string(i);
   assert(r.emit("draw", 100 + t, 10 + t, body));
  }
 });
 for (auto &t : workers) t.join();
 r.stop();
 auto s = r.snapshot();
 assert(!s.active && !s.io_error && s.attempted == s.written && !s.dropped);
 assert(s.draws == 408004 && s.contexts == 7 && s.frames == 4003 && s.files > 1);
 assert(!r.emit("draw", 0, 0));
 assert(!r.start(root / "long")); // never overwrite a session
 // Twenty minutes of logical 60 Hz frames (two draws per frame); this is a
 // volume/correlation simulation, not 20 minutes of real-time gameplay.
 assert(r.start(root / "simulated", 600000, 1048576));
 { correlator sim(r); sim.created(90);
  for (int i = 0; i < 72000; ++i) {
   sim.draw(90, "\"part\":1"); sim.draw(90, "\"part\":2");
   sim.present_begin(90); sim.present(90, true);
  }
  sim.finish();
 }
 r.stop(); s=r.snapshot(); assert(s.frames==72000 && s.draws==144000 && !s.dropped && !s.io_error && !s.partial_frames);
 // One context may render to two drawables; each has its own frame sequence.
 assert(r.start(root / "surfaces", 1000, 4096));
 { correlator surfaces(r); surfaces.created(55);
  auto a = surfaces.surface(1000), b = surfaces.surface(2000); assert(a != b);
  surfaces.draw(55, "\"value\":11", a); surfaces.present(55, true, 1000);
  surfaces.draw(55, "\"value\":12", a); surfaces.draw(55, "\"value\":21", b);
  surfaces.present(55, true, 2000);
  auto crossing = surfaces.begin_draw(55, a);
  surfaces.present(55, true, 1000);
  surfaces.end_draw(crossing, "\"value\":13"); surfaces.finish();
 }
 r.stop(); assert(!r.snapshot().dropped && !r.snapshot().partial_frames);
 // Sync object labels are session-local, do not serialize handles, and
 // follow a known handle across contexts. Reuse after delete gets a new ID.
 assert(r.start(root / "sync", 1000, 4096));
 { correlator sync(r); sync.created(5); sync.created(6);
  sync.sync_event(5, 15, "glFenceSync", "return", 0x12345678, true, false, 7, 0, 1);
  sync.sync_event(6, 16, "glWaitSync", "begin", 0x12345678, false, false, 0, 20, 0);
  sync.sync_event(6, 16, "glClientWaitSync", "return", 0x12345678, false, false, 0, 20, 0x911a);
  sync.sync_event(5, 15, "glDeleteSync", "return", 0x12345678, false, true, 0, 0, 0);
  sync.sync_event(5, 15, "glFenceSync", "return", 0x12345678, true, false, 7, 0, 1);
  sync.sync_event(6, 16, "glWaitSync", "begin", 0x88888888, false, false, 0, 20, 0);
  sync.finish();
 }
 r.stop(); assert(!r.snapshot().dropped);
 assert(r.start(root / "overflow", 1, 512));
 assert(!r.emit("draw", 1, 1, std::string(8193, 'x'))); // deterministic oversized record drop
 for (int i = 0; i < 100000; ++i) r.emit("draw", 1, 1, "\"value\":1");
 r.stop(); s = r.snapshot();
 assert(s.dropped > 0 && !s.io_error && s.attempted == s.written + s.dropped);
 // A draw that entered before Stop must be recorded before session_end.
 assert(runtime_start(root / "runtime"));
 assert(runtime_begin_draw());
 auto ticket = runtime_bind_draw(100, 0); assert(ticket.id);
 std::atomic<bool> stopped{false};
 std::thread stopping([&]{ runtime_stop(); stopped = true; });
 while (runtime_enabled()) std::this_thread::yield();
 assert(!stopped.load());
 const std::string body = "\"test\":123";
 runtime_end_draw(ticket, &body);
 stopping.join();
 auto integrated = runtime_status();
 assert(!integrated.io_error && !integrated.dropped && integrated.draws == 1 && stopped.load());
#ifndef _WIN32
 // Force the second part to a device that rejects writes; a failed capture
 // must be reported instead of claiming successful, complete output.
 if (std::filesystem::exists("/dev/full")) {
  assert(r.start(root / "io-error", 1000, 256));
  std::filesystem::create_symlink("/dev/full", root / "io-error" / "capture-000002.jsonl");
  for (int i = 0; i < 500; ++i) r.emit("draw", 1, 1, "\"value\":1234567890");
  r.stop(); s = r.snapshot(); assert(s.io_error && s.dropped > 0);
 }
#endif
}
