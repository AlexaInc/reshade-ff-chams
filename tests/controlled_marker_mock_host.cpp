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
static bool registered=false, unregistered=false;
extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void*,std::uint32_t v){registered=v==20;return registered;}
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void*){unregistered=true;}
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*,int,const char*){}
template<class T> static T fn(HMODULE m,const char *s){FARPROC p=GetProcAddress(m,s);T x=nullptr;static_assert(sizeof(p)==sizeof(x),"pointer");std::memcpy(&x,&p,sizeof(x));return x;}
static bool exists(const std::wstring &s){return GetFileAttributesW(s.c_str())!=INVALID_FILE_ATTRIBUTES;}
static std::string read_text(const std::wstring &p){std::ifstream f(p.c_str());std::ostringstream s;s<<f.rdbuf();return s.str();}
static bool wait_for(const std::wstring &p,const char *needle,DWORD ms=10000){ULONGLONG t=GetTickCount64();while(GetTickCount64()-t<ms){if(exists(p)&&read_text(p).find(needle)!=std::string::npos)return true;Sleep(10);}return false;}
static void submit(const std::wstring &p,const std::string &payload){std::wstring tmp=p+L".tmp";HANDLE h=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);assert(h!=INVALID_HANDLE_VALUE);DWORD written=0;assert(WriteFile(h,payload.data(),static_cast<DWORD>(payload.size()),&written,nullptr)&&written==payload.size());CloseHandle(h);assert(MoveFileW(tmp.c_str(),p.c_str()));}
static std::wstring locate(const std::wstring &dir,const wchar_t *pattern){WIN32_FIND_DATAW data={};HANDLE h=INVALID_HANDLE_VALUE;auto p=dir+L"\\"+pattern;ULONGLONG t=GetTickCount64();while(GetTickCount64()-t<5000){h=FindFirstFileW(p.c_str(),&data);if(h!=INVALID_HANDLE_VALUE)break;Sleep(10);}assert(h!=INVALID_HANDLE_VALUE);FindClose(h);return dir+L"\\"+data.cFileName;}
int main(){
 WCHAR base[MAX_PATH]={},dir[MAX_PATH]={};DWORD n=GetTempPathW(MAX_PATH,base);assert(n&&n<MAX_PATH);assert(_snwprintf_s(dir,MAX_PATH,_TRUNCATE,L"%sfixture-marker-%lu",base,static_cast<unsigned long>(GetCurrentProcessId()))>=0);assert(CreateDirectoryW(dir,nullptr));assert(SetEnvironmentVariableW(L"TMP",dir));assert(SetEnvironmentVariableW(L"TEMP",dir));
 auto *memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,65536*4,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE|PAGE_WRITECOMBINE));assert(memory);
 char first[129]={},second[129]={};for(unsigned i=0;i<64;i++){std::snprintf(first+i*2,3,"%02x",i+1);std::snprintf(second+i*2,3,"%02x",255-i);memory[128+i]=static_cast<unsigned char>(i+1);}
 WCHAR addr[32]={};assert(_snwprintf_s(addr,32,_TRUNCATE,L"%llx",static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(memory)))>=0);assert(SetEnvironmentVariableW(L"ARENA_MARKER_TEST_ADDRESS",addr));
 HMODULE addon=LoadLibraryW(L"MarkerTest.addon64");assert(addon);auto init=fn<bool(*)(HMODULE,HMODULE)>(addon,"AddonInit");auto uninit=fn<void(*)(HMODULE,HMODULE)>(addon,"AddonUninit");assert(init&&uninit&&init(addon,GetModuleHandleW(nullptr))&&registered);
 std::wstring ready=locate(dir,L"arena-target-marker-ready-*-1.txt");auto pos=ready.find(L"ready-");assert(pos!=std::wstring::npos);std::wstring output=ready;output.replace(pos,6,L"result-");std::wstring trigger=ready;trigger.replace(pos,6,L"trigger-");trigger.replace(trigger.size()-4,4,L".flag");
 Sleep(100);assert(!exists(output));submit(trigger,first);assert(wait_for(output,"state=finished"));auto r=read_text(output);assert(r.find("reason=match")!=std::string::npos);
 char candidate[64]={};std::snprintf(candidate,sizeof(candidate),"first_host_candidate=0x%llx",static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(memory+128)));assert(r.find(candidate)!=std::string::npos);
 std::vector<std::wstring> cleanup={ready,trigger,output};
 for(unsigned stage=1;stage<=3;stage++){
  if(stage==2)for(unsigned i=0;i<64;i++)memory[128+i]=static_cast<unsigned char>(255-i);
  if(stage==3){assert(VirtualFree(memory,0,MEM_RELEASE));memory=nullptr;}
  std::wstring pat=L"arena-target-marker-verify-ready-*-"+std::to_wstring(stage)+L".txt";auto vr=locate(dir,pat.c_str());auto at=vr.find(L"ready-");assert(at!=std::wstring::npos);
  std::wstring vt=vr;vt.replace(at,6,L"trigger-");vt.replace(vt.size()-4,4,L".flag");std::wstring vo=vr;vo.replace(at,6,L"result-");
  submit(vt,stage==1?first:second);assert(wait_for(vo,"expected_match="));auto v=read_text(vo);
  if(stage==1)assert(v.find("initial_match=1 expected_match=1")!=std::string::npos);
  if(stage==2)assert(v.find("initial_match=0 expected_match=1")!=std::string::npos);
  if(stage==3)assert(v.find("expected_match=0")!=std::string::npos);
  cleanup.push_back(vr);cleanup.push_back(vt);cleanup.push_back(vo);
 }
 uninit(addon,GetModuleHandleW(nullptr));assert(unregistered && FreeLibrary(addon));for(const auto &p:cleanup)assert(DeleteFileW(p.c_str()));assert(RemoveDirectoryW(dir));
 std::puts("PASS: gated search, candidate provenance, live mutation and freed-memory checks");
}
