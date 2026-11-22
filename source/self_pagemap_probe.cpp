// Reads pagemap metadata solely for a page allocated by this process. No game access.
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

struct page_info { bool present; bool swapped; std::uint64_t pfn; };
static page_info decode(std::uint64_t e) {
    return { (e & (UINT64_C(1) << 63)) != 0, (e & (UINT64_C(1) << 62)) != 0, e & ((UINT64_C(1) << 55) - 1) };
}
static int self_test() {
    const auto a=decode((UINT64_C(1)<<63)|UINT64_C(0x12345));
    const auto b=decode(UINT64_C(1)<<62);
    const auto c=decode(0);
    if(!a.present || a.swapped || a.pfn != 0x12345 || b.present || !b.swapped || c.present || c.swapped || c.pfn) return 1;
    std::puts("PASS decode of synthetic present, swapped and absent pagemap entries"); return 0;
}
static int cap_sys_admin() {
    FILE *f=std::fopen("/proc/self/status","r"); if(!f) return -1;
    char line[512]; int answer=-1;
    while(std::fgets(line,sizeof(line),f)) {
        if(std::strncmp(line,"CapEff:",7)==0) {
            char *end=nullptr; errno=0; const auto caps=std::strtoull(line+7,&end,16);
            if(errno==0 && end!=line+7) answer=(caps & (UINT64_C(1)<<21)) ? 1 : 0;
            break;
        }
    }
    std::fclose(f);return answer;
}
int main(int argc, char **argv) {
    if(argc==2 && std::strcmp(argv[1],"--self-test")==0) return self_test();
    if(argc!=1) return 2;
    const long ps=sysconf(_SC_PAGESIZE); if(ps<=0){std::puts("state=error stage=pagesize");return 3;}
    auto *page=static_cast<volatile unsigned char *>(mmap(nullptr,static_cast<std::size_t>(ps),PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    if(page==MAP_FAILED){std::printf("state=error stage=mmap errno=%d\n",errno);return 4;}
    // Fault in an anonymous, private page, without opening or reading any other process.
    page[0]=0x5a;page[ps-1]=0xa5;
    const auto va=reinterpret_cast<std::uintptr_t>(page);
    std::printf("state=allocated pid=%ld page_size=%ld guest_va=0x%" PRIxPTR " cap_sys_admin=%d\n",static_cast<long>(getpid()),ps,va,cap_sys_admin());
    const std::uint64_t vpn=static_cast<std::uint64_t>(va)/static_cast<std::uint64_t>(ps);
    if(vpn > static_cast<std::uint64_t>((std::numeric_limits<off_t>::max)()) / 8) {munmap(const_cast<unsigned char *>(page),static_cast<std::size_t>(ps));std::puts("state=error stage=offset");return 5;}
    const off_t offset=static_cast<off_t>(vpn*8);
    const int fd=open("/proc/self/pagemap",O_RDONLY|O_CLOEXEC);
    if(fd<0){int e=errno;munmap(const_cast<unsigned char *>(page),static_cast<std::size_t>(ps));std::printf("state=unavailable stage=open errno=%d\n",e);return 0;}
    std::uint64_t entry=0;const ssize_t n=pread(fd,&entry,sizeof(entry),offset);
    if(n!=static_cast<ssize_t>(sizeof(entry))){int e=errno;close(fd);munmap(const_cast<unsigned char *>(page),static_cast<std::size_t>(ps));std::printf("state=unavailable stage=read bytes=%ld errno=%d\n",static_cast<long>(n),e);return 0;}
    const auto info=decode(entry);
    std::printf("state=read present=%d swapped=%d pfn_nonzero=%d",info.present?1:0,info.swapped?1:0,(info.present && info.pfn!=0)?1:0);
    if(info.present && info.pfn) std::printf(" guest_pfn=0x%" PRIx64,info.pfn);
    std::printf("\n");
    page[0]=0;page[ps-1]=0;const int unmapped=munmap(const_cast<unsigned char *>(page),static_cast<std::size_t>(ps));
    if(unmapped!=0){std::printf("state=error stage=unmap errno=%d\n",errno);close(fd);return 6;}
    std::uint64_t after=0;const ssize_t n2=pread(fd,&after,sizeof(after),offset);close(fd);
    if(n2==static_cast<ssize_t>(sizeof(after))){const auto a=decode(after);std::printf("state=unmapped present=%d swapped=%d\n",a.present?1:0,a.swapped?1:0);}else std::printf("state=unmapped postcheck=unavailable bytes=%ld\n",static_cast<long>(n2));
    return 0;
}
