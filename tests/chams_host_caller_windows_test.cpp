// Real Windows API smoke test: host probe mechanics only, no emulator or GL.
#define NOMINMAX
#define CHAMS_HOST_CALLER_PROBE 1
#include "../source/chams_host_caller_probe.hpp"
#include <cassert>
#include <string>
#include <vector>
static std::vector<std::string> events;
namespace reshade::chams::capture {
 uint64_t runtime_generation() noexcept { return 1; }
 void runtime_inflight_event(uintptr_t c,const char *kind,const std::string &body,uint64_t s) noexcept {
  assert(c==123 && s==7 && std::string(kind)=="host_caller_sample"); events.push_back(body);
 }
 void runtime_record_loss() noexcept { assert(false); }
}
int main() {
 using namespace reshade::chams;
 assert(SetEnvironmentVariableW(L"CHAMS_HOST_CALLER_PROBE", L"1"));
 auto kernel=GetModuleHandleW(L"kernel32.dll"); assert(kernel);
 assert(std::string(caller_probe::known_name(kernel))=="kernel32.dll");
 // R1 split: this module (the test binary) is the "proxy" by handle comparison,
 // never by path; an allowlisted foreign opengl32 keeps the plain name.
 HMODULE self = nullptr;
 GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
  reinterpret_cast<LPCWSTR>(&main), &self);
 assert(self != nullptr);
 assert(std::string(caller_probe::module_label(self, self)) == "reshade_proxy_opengl32.dll");
 assert(std::string(caller_probe::module_label(kernel, self)) == "kernel32.dll");
 assert(caller_probe::module_label(nullptr, self) == nullptr);
 capture::draw_ticket t{}; t.id=42;
 caller_probe::sample(123,7,t);
 assert(events.size()==1);
 assert(events[0].find("\"draw_id\":42")!=std::string::npos);
 assert(events[0].find("\"correlation_status\":\"guest_unobserved\"")!=std::string::npos);
 return 0;
}
