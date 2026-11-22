#pragma once
#include <cstddef>
#include <cstdint>
namespace controlled_marker {
constexpr std::size_t kSize = 64;
constexpr std::size_t kHexSize = kSize * 2;
inline int hex_digit(char c) noexcept {
 if(c>='0'&&c<='9') return c-'0';
 if(c>='a'&&c<='f') return c-'a'+10;
 if(c>='A'&&c<='F') return c-'A'+10;
 return -1;
}
inline bool parse_hex(const char *s, std::size_t n, unsigned char (&out)[kSize]) noexcept {
 if(!s || n!=kHexSize)return false;
 for(std::size_t i=0;i<kSize;i++) {int a=hex_digit(s[i*2]), b=hex_digit(s[i*2+1]);if(a<0||b<0){for(auto &x:out)x=0;return false;}out[i]=static_cast<unsigned char>((a<<4)|b);}
 return true;
}
}
