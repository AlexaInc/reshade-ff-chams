#pragma once
// Experimental host-side call-site evidence ONLY. This is not a guest/host
// renderer bridge and cannot identify a player or prove a graphics handoff.
// Compile with CHAMS_HOST_CALLER_PROBE and set CHAMS_HOST_CALLER_PROBE=1 before
// process start; it runs only while the existing runtime capture is active.
#if defined(_WIN32) && defined(CHAMS_HOST_CALLER_PROBE)
#include "chams_caller_sampling.hpp"
#include "chams_capture.hpp"
#include <windows.h>
#include <cwchar>
#include <cstdint>
#include <string>

namespace reshade::chams::caller_probe
{
 inline bool enabled() noexcept
 {
  static const bool value = [] {
   wchar_t text[4] = {};
   return GetEnvironmentVariableW(L"CHAMS_HOST_CALLER_PROBE", text, 4) == 1 && text[0] == L'1';
  }();
  return value;
 }
 // Do not write paths, unrecognized module names, absolute addresses or stack
 // contents. Known names are fixed literals, not arbitrary process input.
 inline const char *known_name(HMODULE module) noexcept
 {
  wchar_t path[1024] = {};
  const DWORD n = GetModuleFileNameW(module, path, 1024);
  if (!n || n >= 1024) return nullptr;
  const wchar_t *base = std::wcsrchr(path, L'\\'); base = base ? base + 1 : path;
  struct item { const wchar_t *name; const char *label; };
  static const item known[] = {
   {L"HD-Player.exe", "HD-Player.exe"}, {L"HD-Opengl-Native.dll", "HD-Opengl-Native.dll"},
   {L"libOpenglRender.dll", "libOpenglRender.dll"}, {L"libGLES_V2_translator.dll", "libGLES_V2_translator.dll"},
   {L"libGLES_CM_translator.dll", "libGLES_CM_translator.dll"}, {L"libGLESv2.dll", "libGLESv2.dll"},
   {L"libEGL_translator.dll", "libEGL_translator.dll"}, {L"libEGL.dll", "libEGL.dll"},
   {L"opengl32.dll", "opengl32.dll"}, {L"nvoglv64.dll", "nvoglv64.dll"},
   {L"ig9icd64.dll", "ig9icd64.dll"}, {L"atio6axx.dll", "atio6axx.dll"},
   {L"ntdll.dll", "ntdll.dll"}, {L"kernel32.dll", "kernel32.dll"}, {L"kernelbase.dll", "kernelbase.dll"}
  };
  for (const auto &item : known) if (_wcsicmp(base, item.name) == 0) return item.label;
  return nullptr;
 }
 inline void sample(uintptr_t native_context, uint64_t surface, const capture::draw_ticket &ticket) noexcept
 {
  if (!ticket.id || !enabled()) return;
  try {
   static sampler policy(512, 128);
   const auto choice = policy.observe(capture::runtime_generation());
   if (!choice.selected) return;
   void *frames[24] = {};
   const USHORT count = CaptureStackBackTrace(0, 24, frames, nullptr);
   std::string body = "\"draw_id\":" + std::to_string(ticket.id) +
    ",\"ordinal\":" + std::to_string(choice.ordinal) + ",\"sample\":" + std::to_string(choice.sample) +
    ",\"sampling_interval\":512,\"sample_limit\":128,\"returned_frames\":" + std::to_string(count) +
    ",\"stack_may_be_truncated\":" + std::string(count == 24 ? "true" : "false") +
    ",\"correlation_status\":\"guest_unobserved\",\"frames\":[";
   for (USHORT i = 0; i < count; ++i) {
    if (i) body += ',';
    HMODULE module = nullptr;
    const bool found = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
     reinterpret_cast<LPCWSTR>(frames[i]), &module) != 0;
    const char *name = found ? known_name(module) : nullptr;
    if (name) {
     const auto pc = reinterpret_cast<uintptr_t>(frames[i]), base = reinterpret_cast<uintptr_t>(module);
     body += std::string("{\"module\":\"") + name + "\",\"return_rva\":" + std::to_string(pc - base) + "}";
    } else body += found ? "{\"module\":\"other\"}" : "{\"module\":\"unresolved\"}";
   }
   body += ']';
   capture::runtime_inflight_event(native_context, "host_caller_sample", body, surface);
  } catch (...) { capture::runtime_record_loss(); }
 }
}
#endif
