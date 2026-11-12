#pragma once
// Minimal fake Win32 calls for the host probe unit test. Not a Windows runtime.
#include <cstdint>
#include <cwchar>
#include <cwctype>
using DWORD = unsigned long;
using USHORT = unsigned short;
using HMODULE = void *;
using LPCWSTR = const wchar_t *;
constexpr DWORD GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS = 4;
constexpr DWORD GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT = 2;
inline DWORD GetCurrentProcessId() { return 4242; }
inline DWORD GetCurrentThreadId() { return 777; }
extern bool fake_enabled;
extern unsigned long fake_env_reads;
inline DWORD GetEnvironmentVariableW(const wchar_t *, wchar_t *out, DWORD cap) {
 ++fake_env_reads;
 if (!fake_enabled || cap < 2) return 0;
 out[0]=L'1'; out[1]=0; return 1;
}
// Fake module map (module base -> file name):
//   0x70000000 "this" module, i.e. the ReShade proxy -> ...\\BlueStacks_nxt\\opengl32.dll
//   0x80000000 Windows system opengl32.dll             -> ...\\System32\\opengl32.dll
//   0x10000000 libOpenglRender.dll (allowlisted, RVA 0x100)
//   0x20000000 private module (not allowlisted -> "other")
//   0x90000000 libaosl.dll (R1 allowlist addition, RVA 0x100)
// Frame 4 (0x30000300) is deliberately unresolvable -> "unresolved".
// Any address outside these ranges is treated as belonging to this module (the proxy).
inline USHORT CaptureStackBackTrace(DWORD, DWORD, void **frames, DWORD *) {
 frames[0]=reinterpret_cast<void *>(uintptr_t(0x70000180));
 frames[1]=reinterpret_cast<void *>(uintptr_t(0x80000200));
 frames[2]=reinterpret_cast<void *>(uintptr_t(0x10000100));
 frames[3]=reinterpret_cast<void *>(uintptr_t(0x20000200));
 frames[4]=reinterpret_cast<void *>(uintptr_t(0x30000300));
 frames[5]=reinterpret_cast<void *>(uintptr_t(0x90000100));
 return 6;
}
inline int GetModuleHandleExW(DWORD, LPCWSTR pc, HMODULE *module) {
 const auto p=reinterpret_cast<uintptr_t>(pc);
 const auto ranged=[&p](uintptr_t base){ return p>=base && p<base+0x00100000; };
 if (p==0x30000300) return 0;
 if (ranged(0x10000000)) { *module=reinterpret_cast<void *>(uintptr_t(0x10000000)); return 1; }
 if (ranged(0x20000000)) { *module=reinterpret_cast<void *>(uintptr_t(0x20000000)); return 1; }
 if (ranged(0x80000000)) { *module=reinterpret_cast<void *>(uintptr_t(0x80000000)); return 1; }
 if (ranged(0x90000000)) { *module=reinterpret_cast<void *>(uintptr_t(0x90000000)); return 1; }
 *module=reinterpret_cast<void *>(uintptr_t(0x70000000)); return 1;
}
inline DWORD GetModuleFileNameW(HMODULE module, wchar_t *dst, DWORD cap) {
 const auto p=reinterpret_cast<uintptr_t>(module);
 const wchar_t *name =
  p==0x70000000 ? L"C:\\Program Files\\BlueStacks_nxt\\opengl32.dll" :
  p==0x80000000 ? L"C:\\Windows\\System32\\opengl32.dll" :
  p==0x10000000 ? L"C:\\Program Files\\BlueStacks_nxt\\libOpenglRender.dll" :
  p==0x20000000 ? L"C:\\Private\\secret-module.dll" :
  p==0x90000000 ? L"C:\\Program Files\\BlueStacks_nxt\\libaosl.dll" : L"";
 if (!*name) return 0;
 const auto n=std::wcslen(name); if(n+1>cap)return cap;
 std::wcscpy(dst,name); return static_cast<DWORD>(n);
}
inline int _wcsicmp(const wchar_t *a,const wchar_t *b) {
 while (*a && *b) { int d=std::towlower(*a++)-std::towlower(*b++); if(d)return d; }
 return *a-*b;
}
