#include "../source/chams_capture.hpp"
#include <cassert>
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
 assert(r.start(root / "overflow", 1, 512));
 assert(!r.emit("draw", 1, 1, std::string(8193, 'x'))); // deterministic oversized record drop
 for (int i = 0; i < 100000; ++i) r.emit("draw", 1, 1, "\"value\":1");
 r.stop(); s = r.snapshot();
 assert(s.dropped > 0 && !s.io_error && s.attempted == s.written + s.dropped);
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
