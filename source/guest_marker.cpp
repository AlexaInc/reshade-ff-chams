// Controlled guest-owned diagnostic marker. Never opens another process.
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
static bool random_fill(unsigned char *out, std::size_t n) {
 int f=open("/dev/urandom",O_RDONLY|O_CLOEXEC); if(f<0)return false;
 std::size_t done=0; while(done<n) {ssize_t k=read(f,out+done,n-done);if(k<0&&errno==EINTR)continue;if(k<=0){close(f);return false;}done+=static_cast<std::size_t>(k);}close(f);return true;
}
int main(int argc,char **argv) {
 if(argc!=2){std::fprintf(stderr,"usage: guest_marker <alphanumeric-session>\n");return 2;}
 const char *session=argv[1];std::size_t len=std::strlen(session);
 if(len<8||len>32)return 2;
 for(std::size_t i=0;i<len;i++)if(!std::isalnum(static_cast<unsigned char>(session[i])))return 2;
#ifdef MARKER_GUEST_TEST
 const char *base=std::getenv("ARENA_GUEST_MARKER_TEST_DIR");if(!base)return 2;
#else
 const char *base="/data/local/tmp";
#endif
 char rotate[512],stop[512];
 const int nr=std::snprintf(rotate,sizeof(rotate),"%s/arena-marker-%s.rotate",base,session);
 const int ns=std::snprintf(stop,sizeof(stop),"%s/arena-marker-%s.stop",base,session);
 if(nr<0||ns<0||nr>=static_cast<int>(sizeof(rotate))||ns>=static_cast<int>(sizeof(stop)))return 2;
 auto *page=static_cast<unsigned char *>(mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
 if(page==MAP_FAILED){std::perror("mmap");return 3;}
 if(!random_fill(page,64)){munmap(page,4096);return 4;}
 auto report=[&](const char *state,unsigned version) {
  std::printf("state=%s session=%s pid=%ld guest_va=0x%llx version=%u marker=",state,session,static_cast<long>(getpid()),static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(page)),version);
  for(unsigned i=0;i<64;i++)std::printf("%02x",static_cast<unsigned>(page[i]));
  std::printf("\n");std::fflush(stdout);
 };
 std::setvbuf(stdout,nullptr,_IOLBF,0);report("ready",1);unsigned version=1;
 struct timespec started={};clock_gettime(CLOCK_MONOTONIC,&started);
 for(;;) {
  if(access(stop,F_OK)==0){unlink(stop);break;}
  if(access(rotate,F_OK)==0){unlink(rotate);if(!random_fill(page,64))break;report("rotated",++version);}
  struct timespec now={};clock_gettime(CLOCK_MONOTONIC,&now);if(now.tv_sec-started.tv_sec>=900)break;
  struct timespec wait={0,250000000};nanosleep(&wait,nullptr);
 }
 volatile unsigned char *v=page;for(unsigned i=0;i<64;i++)v[i]=0;
 const int rc=munmap(page,4096);std::printf("state=freed session=%s version=%u munmap_ok=%d\n",session,version,rc==0?1:0);std::fflush(stdout);return rc==0?0:5;
}
