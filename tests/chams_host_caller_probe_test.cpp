// Simulates a host stack provider and the recorder; no BlueStacks, Windows,
// GPU, guest events or real renderer/draw correlation are exercised.
#include <cassert>
#include <cstdint>
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

bool fake_enabled = false;
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
int main(int argc, char **argv) {
 using namespace reshade::chams;
 assert(argc==2); fake_enabled = std::string(argv[1])=="on";
 capture::draw_ticket t{}; t.id=42; t.context=3; t.surface=7;
 if (!fake_enabled) {
  caller_probe::sample(123,7,t); assert(emitted.empty()); return 0;
 }
 t.id=0; caller_probe::sample(123,7,t); assert(emitted.empty()); t.id=42;
 caller_probe::sample(123,7,t); assert(emitted.size()==1);
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
 caller_probe::sampler policy(7,10000); std::mutex guard; std::set<uint64_t> selected;
 std::vector<std::thread> threads;
 for(int k=0;k<8;k++) threads.emplace_back([&]{
  for(int j=0;j<1000;j++) { auto x=policy.observe(10); if(x.selected) { std::lock_guard<std::mutex> l(guard); assert(selected.insert(x.ordinal).second); } }
 });
 for(auto &x:threads)x.join();
 assert(selected.size()==1143 && *selected.begin()==1 && *selected.rbegin()==7995);
 assert(!policy.observe(9).selected); assert(policy.observe(11).ordinal==1);
 return 0;
}
