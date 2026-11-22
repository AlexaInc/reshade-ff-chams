// Opt-in controlled guest marker probe. Reads only this host process.
#include "controlled_marker_scan.hpp"
#include "marker_protocol.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
extern "C" {
__declspec(dllexport) const char *NAME = "Five Batch Guest Marker Probe";
__declspec(dllexport) const char *DESCRIPTION = "Bounded opt-in synthetic marker search and candidate verification.";
}
template<class T> static T api_function(HMODULE m,const char *name) noexcept {FARPROC p=GetProcAddress(m,name);T out=nullptr;static_assert(sizeof(out)==sizeof(p),"pointer size");std::memcpy(&out,&p,sizeof(out));return out;}
using register_fn=bool(*)(void*,std::uint32_t);using unregister_fn=void(*)(void*);using log_fn=void(*)(void*,int,const char*);
struct context { HMODULE module=nullptr; HANDLE stop=nullptr; ULONGLONG token=0; };
static context g_ctx;static HANDLE g_stop=nullptr;static HANDLE g_thread=nullptr;
#ifdef MARKER_TEST
constexpr unsigned kSlices=2; constexpr std::size_t kBudget=65536; constexpr DWORD kChunkPause=30,kSlicePause=0,kPoll=10;
#else
constexpr unsigned kSlices=8; constexpr std::size_t kBudget=64u*1024u*1024u; constexpr DWORD kChunkPause=4,kSlicePause=2000,kPoll=1000;
#endif
constexpr unsigned kMaxBatches=5;
static bool path(WCHAR (&out)[MAX_PATH],const WCHAR *fmt,DWORD pid,ULONGLONG token,unsigned index) noexcept {
 DWORD n=GetTempPathW(MAX_PATH,out);return n>0&&n<MAX_PATH&&_snwprintf_s(out+n,MAX_PATH-n,_TRUNCATE,fmt,static_cast<unsigned long>(pid),static_cast<unsigned long long>(token),index)>=0;
}
static bool write_new(const WCHAR *p,const char *s) noexcept {
 HANDLE f=CreateFileW(p,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return false;
 DWORD n=static_cast<DWORD>(std::strlen(s)),w=0;BOOL ok=WriteFile(f,s,n,&w,nullptr);CloseHandle(f);return ok&&w==n;
}
static bool wait_file(const WCHAR *p,HANDLE stop) noexcept {
 for(;;){if(WaitForSingleObject(stop,kPoll)!=WAIT_TIMEOUT)return false;DWORD a=GetFileAttributesW(p);if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY)==0)return true;}
}
static bool read_marker(const WCHAR *p,unsigned char (&marker)[controlled_marker::kSize]) noexcept {
 // Limit to 128 hex bytes; raw target marker itself is never read from host memory here.
 HANDLE f=CreateFileW(p,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return false;
 LARGE_INTEGER size={};char hex[controlled_marker::kHexSize]={};DWORD got=0;
 const bool ok=GetFileSizeEx(f,&size)&&size.QuadPart==static_cast<LONGLONG>(sizeof(hex))&&ReadFile(f,hex,sizeof(hex),&got,nullptr)&&got==sizeof(hex)&&controlled_marker::parse_hex(hex,got,marker);
 CloseHandle(f);SecureZeroMemory(hex,sizeof(hex));return ok;
}
static DWORD WINAPI worker(LPVOID arg) noexcept {
 const auto *ctx=static_cast<context *>(arg);const DWORD pid=GetCurrentProcessId();
 SYSTEM_INFO si={};GetSystemInfo(&si);auto end=reinterpret_cast<std::uintptr_t>(si.lpMaximumApplicationAddress);
 std::uintptr_t cursor=reinterpret_cast<std::uintptr_t>(si.lpMinimumApplicationAddress),candidate=0;
#ifdef MARKER_TEST
 // Test DLL only: restrict scans to the mock host-owned allocation.
 WCHAR range[64]={};if(!GetEnvironmentVariableW(L"ARENA_MARKER_TEST_ADDRESS",range,64))return 11;
 cursor=static_cast<std::uintptr_t>(_wcstoui64(range,nullptr,16));
 MEMORY_BASIC_INFORMATION fixture={};if(!VirtualQuery(reinterpret_cast<const void *>(cursor),&fixture,sizeof(fixture))||fixture.State!=MEM_COMMIT)return 12;
 end=cursor+fixture.RegionSize;
#endif
 unsigned char marker[controlled_marker::kSize]={};
 for(unsigned batch=1;batch<=kMaxBatches;batch++) {
  WCHAR ready[MAX_PATH]={},trigger[MAX_PATH]={},output[MAX_PATH]={};
  if(!path(ready,L"arena-target5-marker-ready-%lu-%llu-%u.txt",pid,ctx->token,batch)||!path(trigger,L"arena-target5-marker-trigger-%lu-%llu-%u.flag",pid,ctx->token,batch)||!path(output,L"arena-target5-marker-result-%lu-%llu-%u.txt",pid,ctx->token,batch))return 1;
  char msg[256]={};std::snprintf(msg,sizeof(msg),"ready pid=%lu token=%llu batch=%u max_batches=%u begin=0x%llx slices=%u slice_bytes=%llu filter=private_rw_writecombine\r\n",static_cast<unsigned long>(pid),static_cast<unsigned long long>(ctx->token),batch,kMaxBatches,static_cast<unsigned long long>(cursor),kSlices,static_cast<unsigned long long>(kBudget));
  if(!write_new(ready,msg)||!wait_file(trigger,ctx->stop))return 2;
  if(batch==1 && !read_marker(trigger,marker)){write_new(output,"[TARGET_GUEST_MARKER] state=finished reason=invalid_marker\r\n");return 3;}
  HANDLE f=CreateFileW(output,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return 4;
  auto append=[f](const char *s) noexcept {DWORD n=static_cast<DWORD>(std::strlen(s)),w=0;return WriteFile(f,s,n,&w,nullptr)&&w==n;};
  std::snprintf(msg,sizeof(msg),"[TARGET_GUEST_MARKER] state=start batch=%u begin=0x%llx\r\n",batch,static_cast<unsigned long long>(cursor));if(!append(msg)){CloseHandle(f);return 5;}
  const char *reason="slice_limit";
  for(unsigned slice=1;slice<=kSlices;slice++) {
   controlled_marker_scan::scan_options o;o.signature=marker;o.begin=cursor;o.end=end;o.budget=kBudget;o.pause_ms=kChunkPause;o.cancel=ctx->stop;o.exclude_module=ctx->module;
   o.private_writecombine_only=true;
   auto r=controlled_marker_scan::scan(o);char line[512]={};
   std::snprintf(line,sizeof(line),"[TARGET_GUEST_MARKER] batch=%u slice=%u begin=0x%llx next=0x%llx attempted=%llu copied=%llu regions=%llu read_failures=%llu matches=%llu first_host_candidate=0x%llx limit=%d timeout=%d cancelled=%d allocation_failed=%d\r\n",batch,slice,static_cast<unsigned long long>(cursor),static_cast<unsigned long long>(r.next_cursor),static_cast<unsigned long long>(r.attempted),static_cast<unsigned long long>(r.copied),static_cast<unsigned long long>(r.regions),static_cast<unsigned long long>(r.failures),static_cast<unsigned long long>(r.matches),static_cast<unsigned long long>(r.first_match),r.limit_reached?1:0,r.time_reached?1:0,r.cancelled?1:0,r.allocation_failed?1:0);
   if(!append(line)){reason="output_error";break;}
   if(r.cancelled){reason="cancelled";break;}if(r.allocation_failed){reason="allocation_failed";break;}
   if(r.matches){candidate=r.first_match;cursor=r.next_cursor;reason="match";break;}
   if(r.next_cursor<=cursor){reason="stalled";break;}cursor=r.next_cursor;if(cursor>=end){reason="range_complete";break;}
   if(slice<kSlices && WaitForSingleObject(ctx->stop,kSlicePause)!=WAIT_TIMEOUT){reason="cancelled";break;}
  }
  std::snprintf(msg,sizeof(msg),"[TARGET_GUEST_MARKER] state=finished batch=%u reason=%s next=0x%llx\r\n",batch,reason,static_cast<unsigned long long>(cursor));bool written=append(msg);CloseHandle(f);
  if(!written)return 6;
  if(candidate)break;
  if(std::strcmp(reason,"slice_limit")!=0)return 0;
 }
 if(!candidate)return 0; // No arbitrary host-address verification.
 // Only the first candidate discovered above may be re-read, for at most three
 // explicitly triggered comparisons (baseline, after rotate, after unmap).
 for(unsigned stage=1;stage<=3;stage++) {
  WCHAR ready[MAX_PATH]={},trigger[MAX_PATH]={},out[MAX_PATH]={};
  if(!path(ready,L"arena-target5-marker-verify-ready-%lu-%llu-%u.txt",pid,ctx->token,stage)||!path(trigger,L"arena-target5-marker-verify-trigger-%lu-%llu-%u.flag",pid,ctx->token,stage)||!path(out,L"arena-target5-marker-verify-result-%lu-%llu-%u.txt",pid,ctx->token,stage))return 7;
  char msg[256]={};std::snprintf(msg,sizeof(msg),"ready pid=%lu token=%llu stage=%u candidate=0x%llx\r\n",static_cast<unsigned long>(pid),static_cast<unsigned long long>(ctx->token),stage,static_cast<unsigned long long>(candidate));
  if(!write_new(ready,msg)||!wait_file(trigger,ctx->stop))return 8;
  unsigned char expected[controlled_marker::kSize]={},observed[controlled_marker::kSize]={};
  if(!read_marker(trigger,expected)){write_new(out,"[TARGET_GUEST_MARKER_VERIFY] invalid_marker\r\n");return 9;}
  SIZE_T copied=0;BOOL ok=ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void *>(candidate),observed,sizeof(observed),&copied);
  const bool full=ok&&copied==sizeof(observed),old_match=full&&std::memcmp(observed,marker,sizeof(marker))==0,new_match=full&&std::memcmp(observed,expected,sizeof(expected))==0;
  std::snprintf(msg,sizeof(msg),"[TARGET_GUEST_MARKER_VERIFY] stage=%u readable=%d copied=%llu initial_match=%d expected_match=%d\r\n",stage,full?1:0,static_cast<unsigned long long>(copied),old_match?1:0,new_match?1:0);
  SecureZeroMemory(expected,sizeof(expected));SecureZeroMemory(observed,sizeof(observed));if(!write_new(out,msg))return 10;
 }
 SecureZeroMemory(marker,sizeof(marker));return 0;
}
extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon,HMODULE reshade) {
 if(!addon||!reshade)return false;
 auto reg=api_function<register_fn>(reshade,"ReShadeRegisterAddon");if(!reg||!reg(addon,20))return false;
 if(!g_thread){g_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);LARGE_INTEGER tick={};QueryPerformanceCounter(&tick);if(g_stop){g_ctx={addon,g_stop,static_cast<ULONGLONG>(tick.QuadPart)};g_thread=CreateThread(nullptr,0,worker,&g_ctx,0,nullptr);if(!g_thread){CloseHandle(g_stop);g_stop=nullptr;}}}
 auto log=api_function<log_fn>(reshade,"ReShadeLogMessage");if(log)log(addon,g_thread?3:1,g_thread?"[TARGET_GUEST_MARKER] armed; no scan until trigger":"[TARGET_GUEST_MARKER] worker setup failed");return true;
}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon,HMODULE reshade) {
 if(g_stop)SetEvent(g_stop);
 if(g_thread){WaitForSingleObject(g_thread,INFINITE);CloseHandle(g_thread);g_thread=nullptr;}if(g_stop){CloseHandle(g_stop);g_stop=nullptr;}
 if(!addon||!reshade)return;
 auto unreg=api_function<unregister_fn>(reshade,"ReShadeUnregisterAddon");if(unreg)unreg(addon);
}
