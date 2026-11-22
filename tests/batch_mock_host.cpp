#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
static bool registered = false, unregistered = false;
extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void *, std::uint32_t version) { registered = version == 20; return registered; }
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void *) { unregistered = true; }
extern "C" __declspec(dllexport) void ReShadeLogMessage(void *, int, const char *) {}
template<class T> static T fn(HMODULE m, const char *name) {
 FARPROC raw=GetProcAddress(m,name); T result=nullptr; static_assert(sizeof(result)==sizeof(raw), "Windows pointer"); std::memcpy(&result,&raw,sizeof(result)); return result;
}
static bool exists(const std::wstring &s) { return GetFileAttributesW(s.c_str()) != INVALID_FILE_ATTRIBUTES; }
static std::string text(const std::wstring &s) { std::ifstream f(s.c_str()); std::ostringstream out; out<<f.rdbuf(); return out.str(); }
static bool wait_text(const std::wstring &p,const char *needle,DWORD ms) {
 ULONGLONG start=GetTickCount64();while(GetTickCount64()-start<ms) { if(exists(p) && text(p).find(needle)!=std::string::npos)return true; Sleep(20); } return false;
}
static std::string field(const std::string &s,const char *name) {
 std::string key=std::string(name)+"=";auto p=s.find(key);assert(p!=std::string::npos);p+=key.size();auto end=s.find_first_of(" \r\n",p);return s.substr(p,end-p);
}
int main() {
 WCHAR tmp[MAX_PATH]={}, dir[MAX_PATH]={};DWORD n=GetTempPathW(MAX_PATH,tmp);assert(n&&n<MAX_PATH);
 assert(_snwprintf_s(dir,MAX_PATH,_TRUNCATE,L"%sfixture-batch-%lu",tmp,static_cast<unsigned long>(GetCurrentProcessId()))>=0);
 assert(CreateDirectoryW(dir,nullptr));assert(SetEnvironmentVariableW(L"TMP",dir));assert(SetEnvironmentVariableW(L"TEMP",dir));
 HMODULE addon=LoadLibraryW(L"MemoryBatchTest.addon64");assert(addon);
 auto init=fn<bool(*)(HMODULE,HMODULE)>(addon,"AddonInit");auto uninit=fn<void(*)(HMODULE,HMODULE)>(addon,"AddonUninit");
 assert(init && uninit && init(addon,GetModuleHandleW(nullptr)) && registered);
 WIN32_FIND_DATAW data={};HANDLE h=INVALID_HANDLE_VALUE; ULONGLONG start=GetTickCount64();
 std::wstring pattern=std::wstring(dir)+L"\\chams-batch-ready-*-1.txt";
 while(GetTickCount64()-start<5000) {h=FindFirstFileW(pattern.c_str(),&data);if(h!=INVALID_HANDLE_VALUE)break;Sleep(20);}
 assert(h!=INVALID_HANDLE_VALUE);FindClose(h);
 std::wstring ready1=std::wstring(dir)+L"\\"+data.cFileName;
 assert(ready1.size()>6 && ready1.substr(ready1.size()-6)==L"-1.txt");
 std::wstring stem=ready1.substr(0,ready1.size()-6);
 std::string previous_next; std::vector<std::wstring> cleanup;
 for(int i=1;i<=4;i++) {
  std::wstring ready=stem+L"-"+std::to_wstring(i)+L".txt";
  assert(wait_text(ready,"ready ",5000)); std::string info=text(ready);
  if(i>1)assert(field(info,"begin")==previous_next);
  auto pos=ready.find(L"ready-");assert(pos!=std::wstring::npos);
  std::wstring output=ready;output.replace(pos,6,L"result-");
  std::wstring trigger=ready;trigger.replace(pos,6,L"trigger-");trigger.replace(trigger.size()-4,4,L".flag");
  Sleep(150);assert(!exists(output)); // no scan until THIS batch trigger
  HANDLE flag=CreateFileW(trigger.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);assert(flag!=INVALID_HANDLE_VALUE);CloseHandle(flag);
  assert(wait_text(output,"state=finished",15000));std::string result=text(output);
  assert(result.find("reason=slice_limit")!=std::string::npos);
  auto finish=result.find("state=finished");previous_next=field(result.substr(finish),"next");assert(previous_next!=field(info,"begin"));
  cleanup.push_back(trigger);cleanup.push_back(output);cleanup.push_back(ready);
 }
 Sleep(100);assert(!exists(stem+L"-5.txt"));
 uninit(addon,GetModuleHandleW(nullptr));assert(unregistered && FreeLibrary(addon));for(const auto &p:cleanup)assert(DeleteFileW(p.c_str()));
 // Reload and cancel an in-flight batch; no second batch should be armed.
 registered=false;unregistered=false;addon=LoadLibraryW(L"MemoryBatchTest.addon64");assert(addon);
 init=fn<bool(*)(HMODULE,HMODULE)>(addon,"AddonInit");uninit=fn<void(*)(HMODULE,HMODULE)>(addon,"AddonUninit");assert(init && uninit && init(addon,GetModuleHandleW(nullptr)));
 h=INVALID_HANDLE_VALUE;start=GetTickCount64();while(GetTickCount64()-start<5000) {h=FindFirstFileW(pattern.c_str(),&data);if(h!=INVALID_HANDLE_VALUE)break;Sleep(10);}assert(h!=INVALID_HANDLE_VALUE);FindClose(h);
 ready1=std::wstring(dir)+L"\\"+data.cFileName;auto pos=ready1.find(L"ready-");assert(pos!=std::wstring::npos);
 std::wstring trigger=ready1;trigger.replace(pos,6,L"trigger-");trigger.replace(trigger.size()-4,4,L".flag");
 std::wstring output=ready1;output.replace(pos,6,L"result-");
 HANDLE flag=CreateFileW(trigger.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);assert(flag!=INVALID_HANDLE_VALUE);CloseHandle(flag);
 assert(wait_text(output,"state=start",5000));uninit(addon,GetModuleHandleW(nullptr));assert(unregistered && FreeLibrary(addon));
 assert(text(output).find("reason=cancelled")!=std::string::npos);assert(!exists(ready1.substr(0,ready1.size()-6)+L"-2.txt"));
 assert(DeleteFileW(trigger.c_str())&&DeleteFileW(output.c_str())&&DeleteFileW(ready1.c_str()));assert(RemoveDirectoryW(dir));
 std::puts("PASS: four gated batches, cursor continuity, cap, and cancellation/unload");
}
