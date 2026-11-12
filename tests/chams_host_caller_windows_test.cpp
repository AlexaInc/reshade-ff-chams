// Real Windows API smoke test: host probe mechanics only, no emulator or GL.
#define NOMINMAX
#define CHAMS_HOST_CALLER_PROBE 1
#include "../source/chams_host_caller_probe.hpp"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
static std::vector<std::string> events;
// Stub for the once-per-process probe state log line (Option A) and the D1 gate lines.
namespace reshade::log { static int log_calls = 0; static char log_text[512] = {}; static std::vector<std::string> all_lines;
 void message(level, const char *format, ...) { char buffer[512] = {}; va_list args; va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
  if (log_calls == 0) std::snprintf(log_text, sizeof(log_text), "%s", buffer); ++log_calls; all_lines.emplace_back(buffer); } }
namespace reshade::chams::capture {
 uint64_t runtime_generation() noexcept { return 1; }
 void runtime_inflight_event(uintptr_t c,const char *kind,const std::string &body,uint64_t s) noexcept {
  assert(c==123 && s==7 && std::string(kind)=="host_caller_sample"); events.push_back(body);
 }
 void runtime_record_loss() noexcept { assert(false); }
}
int main() {
 using namespace reshade::chams;
 // D3: the probe build self-activates. Remove the variable and verify it is
 // absent; sampling must still happen.
 assert(SetEnvironmentVariableW(L"CHAMS_HOST_CALLER_PROBE", nullptr));
 {
  wchar_t probe_env[4] = {};
  assert(GetEnvironmentVariableW(L"CHAMS_HOST_CALLER_PROBE", probe_env, 4) == 0);
 }
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
 // Probe state line: exactly one per process; D3 makes the probe build self-activating, so it always reads enabled=1.
 assert(reshade::log::log_calls == 1);
 assert(std::string(reshade::log::log_text).find("build=R1-A") != std::string::npos);
 assert(std::string(reshade::log::log_text).find("enabled=1") != std::string::npos);
 caller_probe::sample(123,7,t);
 assert(reshade::log::log_calls == 1);
 assert(events[0].find("\"draw_id\":42")!=std::string::npos);
 assert(events[0].find("\"correlation_status\":\"guest_unobserved\"")!=std::string::npos);
 // D1 diagnostics: exactly once per process, with PID/TID.
 const int before = reshade::log::log_calls;
 caller_probe::note_gate_entry(); caller_probe::note_gate_entry();
 caller_probe::note_gate_disabled(); caller_probe::note_gate_disabled();
 caller_probe::note_begin_draw_failed(); caller_probe::note_begin_draw_failed();
 assert(reshade::log::log_calls == before + 3);
 const auto &first = reshade::log::all_lines[reshade::log::all_lines.size() - 3];
 const auto &second = reshade::log::all_lines[reshade::log::all_lines.size() - 2];
 const auto &third = reshade::log::all_lines[reshade::log::all_lines.size() - 1];
 const DWORD pid = GetCurrentProcessId(), tid = GetCurrentThreadId();
 char expect_pid[64] = {}, expect_tid[64] = {};
 std::snprintf(expect_pid, sizeof(expect_pid), "pid=%lu", static_cast<unsigned long>(pid));
 std::snprintf(expect_tid, sizeof(expect_tid), "tid=%lu", static_cast<unsigned long>(tid));
 assert(first.find("probe-gate entry") != std::string::npos && first.find(expect_pid) != std::string::npos && first.find(expect_tid) != std::string::npos);
 assert(second.find("probe-gate g1: enabled=0") != std::string::npos && second.find(expect_pid) != std::string::npos && second.find(expect_tid) != std::string::npos);
 assert(third.find("probe-gate g2: begin_draw=0") != std::string::npos && third.find(expect_pid) != std::string::npos && third.find(expect_tid) != std::string::npos);
 // D2: record file carries time/PID/TID/paths/open-status and works even with logging unavailable.
 std::error_code ec;
 const auto temp = std::filesystem::temp_directory_path(ec);
 assert(!ec);
 const auto d2 = temp / (L"chams-d2-record-test-" + std::to_wstring(pid) + L".txt");
 assert(caller_probe::write_log_target_record(d2.string().c_str(), "C:\\BlueStacks_nxt\\opengl32.dll", "C:\\BlueStacks_nxt\\ReShade.log", true));
 {
  std::ifstream in(d2, std::ios::binary); const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  assert(contents.find(expect_pid) != std::string::npos && contents.find(expect_tid) != std::string::npos);
  assert(contents.find("self=\"C:\\BlueStacks_nxt\\opengl32.dll\"") != std::string::npos);
  assert(contents.find("log=\"C:\\BlueStacks_nxt\\ReShade.log\"") != std::string::npos);
  assert(contents.find("open=1") != std::string::npos && contents.back() == '\n' && contents.find('\n') == contents.size() - 1);
 }
 assert(caller_probe::write_log_target_record(d2.string().c_str(), "SELF", nullptr, false));
 {
  std::ifstream in(d2, std::ios::binary); const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  assert(contents.find("log=\"\"") != std::string::npos && contents.find("open=0") != std::string::npos);
 }
 std::filesystem::remove(d2, ec);
 return 0;
}
