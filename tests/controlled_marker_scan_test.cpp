#include "../source/controlled_marker_scan.hpp"
#include "../source/marker_protocol.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace controlled_marker_scan;
int main(){
 char hex[128];for(unsigned i=0;i<64;i++){hex[2*i]='a';hex[2*i+1]="0123456789abcdef"[i%16];}
 unsigned char sig[64]={};assert(controlled_marker::parse_hex(hex,sizeof(hex),sig));assert(!controlled_marker::parse_hex(hex,127,sig));
 assert(controlled_marker::parse_hex(hex,sizeof(hex),sig));hex[7]='?';assert(!controlled_marker::parse_hex(hex,sizeof(hex),sig));
 for(auto c:sig)assert(c==0);hex[7]='3';assert(controlled_marker::parse_hex(hex,sizeof(hex),sig));
 auto *target=static_cast<unsigned char*>(VirtualAlloc(nullptr,kChunk*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(target);
 std::memset(target,0,kChunk*2);std::memcpy(target+kChunk-20,sig,64);std::memcpy(target+kChunk+500,sig,64);
 scan_options o;o.signature=sig;o.begin=reinterpret_cast<std::uintptr_t>(target);o.end=o.begin+kChunk*2;o.budget=kChunk*2;o.pause_ms=0;
 auto full=scan(o);assert(full.matches==2 && full.first_match==o.begin+kChunk-20);
 o.budget=kChunk;auto first=scan(o);assert(first.matches==0 && first.limit_reached);o.begin=first.next_cursor;o.budget=kChunk*2;auto next=scan(o);assert(next.matches==2);
 MEMORY_BASIC_INFORMATION own={};assert(VirtualQuery(sig,&own,sizeof(own)));o.begin=reinterpret_cast<std::uintptr_t>(own.BaseAddress);o.end=o.begin+own.RegionSize;o.budget=(std::max)(static_cast<std::size_t>(128),static_cast<std::size_t>(own.RegionSize));auto self=scan(o);assert(self.matches==0 && self.attempted==0);
 assert(VirtualFree(target,0,MEM_RELEASE));std::puts("PASS: strict marker parser, cross-boundary matching, resume and target-allocation self-exclusion");
}
