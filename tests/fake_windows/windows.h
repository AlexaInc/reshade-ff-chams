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
extern bool fake_enabled;
inline DWORD GetEnvironmentVariableW(const wchar_t *, wchar_t *out, DWORD cap) {
 if (!fake_enabled || cap < 2) return 0;
 out[0]=L'1'; out[1]=0; return 1;
}
inline USHORT CaptureStackBackTrace(DWORD, DWORD, void **frames, DWORD *) {
 frames[0]=reinterpret_cast<void *>(uintptr_t(0x10000100));
 frames[1]=reinterpret_cast<void *>(uintptr_t(0x20000200));
 frames[2]=reinterpret_cast<void *>(uintptr_t(0x30000300));
 return 3;
}
inline int GetModuleHandleExW(DWORD, LPCWSTR pc, HMODULE *module) {
 const auto p=reinterpret_cast<uintptr_t>(pc);
 if (p==0x10000100) { *module=reinterpret_cast<void *>(uintptr_t(0x10000000)); return 1; }
 if (p==0x20000200) { *module=reinterpret_cast<void *>(uintptr_t(0x20000000)); return 1; }
 return 0;
}
inline DWORD GetModuleFileNameW(HMODULE module, wchar_t *dst, DWORD cap) {
 const auto p=reinterpret_cast<uintptr_t>(module);
 const wchar_t *name=p==0x10000000 ? L"C:\\Program Files\\BlueStacks_nxt\\libOpenglRender.dll" : L"C:\\Private\\secret-module.dll";
 const auto n=std::wcslen(name); if(n+1>cap)return cap;
 std::wcscpy(dst,name); return static_cast<DWORD>(n);
}
inline int _wcsicmp(const wchar_t *a,const wchar_t *b) {
 while (*a && *b) { int d=std::towlower(*a++)-std::towlower(*b++); if(d)return d; }
 return *a-*b;
}
