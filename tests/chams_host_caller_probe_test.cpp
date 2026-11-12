// Simulates a host stack provider and the recorder; no BlueStacks, Windows,
// GPU, guest events or real renderer/draw correlation are exercised.
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "../source/chams_capture.hpp"
#include "../source/chams_caller_sampling.hpp"
#define _WIN32 1
#define CHAMS_HOST_CALLER_PROBE 1
#include "../source/chams_host_caller_probe.hpp"

// Stub for the once-per-process probe state log line (Option A).
namespace reshade::log {
 std::vector<std::string> log_lines;
 void message(level, const char *format, ...) {
  char buffer[512] = {}; va_list args; va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
  log_lines.emplace_back(buffer);
 }
}

bool fake_enabled = false; // env var state; irrelevant after D3 (probe self-activates)
unsigned long fake_env_reads = 0; // counts GetEnvironmentVariableW calls; must stay 0 in D3
static uint64_t fake_generation = 1;
static std::vector<std::string> emitted;
static unsigned losses = 0;
namespace reshade::chams::capture {
 uint64_t runtime_generation() noexcept { return fake_generation; }
 void runtime_inflight_event(uintptr_t native, const char *kind, const std::string &body, uint64_t surface) noexcept {
  assert(native==123 && surface==7 && std::string(kind)=="host_caller_sample"); emitted.push_back(body);
 }
 void runtime_record_loss() noexcept { ++losses; }
}
int main() {
 using namespace reshade::chams;
 // D3: the probe build must self-activate. The fake harness reports no
 // CHAMS_HOST_CALLER_PROBE (fake_enabled=false), so a sampled draw proves
 // activation without the environment variable — and the env-read counter
 // proves the variable is never consulted.
 fake_enabled = false;
 capture::draw_ticket t{}; t.id=42; t.context=3; t.surface=7;
 t.id=0; caller_probe::sample(123,7,t); assert(emitted.empty()); t.id=42;
 caller_probe::sample(123,7,t); assert(emitted.size()==1);
 assert(fake_env_reads==0);
 // Option A: the probe state line is logged exactly once, with the real gate result.
 assert(reshade::log::log_lines.size()==1);
 assert(reshade::log::log_lines[0].find("build=R1-A")!=std::string::npos);
 assert(reshade::log::log_lines[0].find("enabled=1")!=std::string::npos);
 auto &s=emitted[0];
 assert(s.find("\"draw_id\":42")!=s.npos && s.find("\"correlation_status\":\"guest_unobserved\"")!=s.npos);
 assert(s.find("\"returned_frames\":6")!=s.npos);
 assert(s.find("reshade_proxy_opengl32.dll")!=s.npos && s.find("\"return_rva\":384")!=s.npos);
 assert(s.find("opengl32.dll(win)")!=s.npos && s.find("\"return_rva\":512")!=s.npos);
 assert(s.find("libOpenglRender.dll")!=s.npos && s.find("\"return_rva\":256")!=s.npos);
 assert(s.find("\"module\":\"other\"")!=s.npos && s.find("\"module\":\"unresolved\"")!=s.npos);
 assert(s.find("secret-module")==s.npos && s.find("Private")==s.npos && s.find("Program Files")==s.npos);
 assert(s.find("System32")==s.npos && s.find("BlueStacks")==s.npos);
 assert(s.find("libaosl.dll")!=s.npos); // R1 allowlist label appears, path does not
 for(int i=0;i<512*130;i++) caller_probe::sample(123,7,t);
 assert(emitted.size()==128 && losses==0);
 fake_generation=2; caller_probe::sample(123,7,t); assert(emitted.size()==129);
 fake_generation=1; caller_probe::sample(123,7,t); assert(emitted.size()==129); // stale session
 assert(fake_env_reads==0); // still never read the environment
 assert(reshade::log::log_lines.size()==1); // still exactly one log line per process
 caller_probe::sampler policy(7,10000); std::mutex guard; std::set<uint64_t> selected;
 std::vector<std::thread> threads;
 for(int k=0;k<8;k++) threads.emplace_back([&]{
  for(int j=0;j<1000;j++) { auto x=policy.observe(10); if(x.selected) { std::lock_guard<std::mutex> l(guard); assert(selected.insert(x.ordinal).second); } }
 });
 for(auto &x:threads)x.join();
 assert(selected.size()==1143 && *selected.begin()==1 && *selected.rbegin()==7995);
 assert(!policy.observe(9).selected); assert(policy.observe(11).ordinal==1);
 // D1/D2 diagnostics: exactly once per process, PID/TID present, independent of gates.
 reshade::log::log_lines.clear();
 caller_probe::note_gate_entry(); caller_probe::note_gate_entry(); caller_probe::note_gate_entry();
 caller_probe::note_gate_disabled(); caller_probe::note_gate_disabled();
 caller_probe::note_begin_draw_failed(); caller_probe::note_begin_draw_failed();
 assert(reshade::log::log_lines.size()==3);
 assert(reshade::log::log_lines[0].find("probe-gate entry")!=std::string::npos);
 assert(reshade::log::log_lines[0].find("pid=4242")!=std::string::npos && reshade::log::log_lines[0].find("tid=777")!=std::string::npos);
 assert(reshade::log::log_lines[1].find("probe-gate g1: enabled=0")!=std::string::npos);
 assert(reshade::log::log_lines[1].find("pid=4242")!=std::string::npos && reshade::log::log_lines[1].find("tid=777")!=std::string::npos);
 assert(reshade::log::log_lines[2].find("probe-gate g2: begin_draw=0")!=std::string::npos);
 assert(reshade::log::log_lines[2].find("pid=4242")!=std::string::npos && reshade::log::log_lines[2].find("tid=777")!=std::string::npos);
 // D2: record file is written with the requested paths and open status.
 std::error_code ec;
 const auto temp = std::filesystem::temp_directory_path(ec);
 assert(!ec);
 const std::string d2 = (temp / "chams-d2-record-test.txt").string();
 assert(caller_probe::write_log_target_record(d2.c_str(), "C:\\BlueStacks_nxt\\opengl32.dll", "C:\\BlueStacks_nxt\\ReShade.log", true));
 {
  std::ifstream in(d2); std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  assert(contents.find("pid=4242")!=std::string::npos && contents.find("tid=777")!=std::string::npos);
  assert(contents.find("self=\"C:\\BlueStacks_nxt\\opengl32.dll\"")!=std::string::npos);
  assert(contents.find("log=\"C:\\BlueStacks_nxt\\ReShade.log\"")!=std::string::npos);
  assert(contents.find("open=1")!=std::string::npos);
  assert(contents.find('\n')==contents.size()-1); // exactly one line
 }
 assert(caller_probe::write_log_target_record(d2.c_str(), "SELF", nullptr, false)); // no log resolved (logging disabled)
 {
  std::ifstream in(d2); std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  assert(contents.find("log=\"\"")!=std::string::npos && contents.find("open=0")!=std::string::npos);
 }
 assert(caller_probe::write_log_target_record(d2.c_str(), "bad\"path\r\n", "bad\"log\n", true));
 {
  std::ifstream in(d2); std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  assert(contents.find('\n')==contents.size()-1); // sanitized: still one line
  assert(contents.find("bad'path")!=std::string::npos && contents.find('\r')==std::string::npos);
 }
 assert(!caller_probe::write_log_target_record((temp / "no-such-dir-xyz" / "x.txt").string().c_str(), "S", "L", true));
 std::filesystem::remove(d2, ec);
 return 0;
}
