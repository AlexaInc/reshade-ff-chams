#include "../source/chams_config_validation.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
using reshade::chams::valid_config_text;
int main(int argc, char **argv)
{
 assert(argc == 2);
 std::ifstream f(argv[1], std::ios::binary); assert(f);
 const std::string provided((std::istreambuf_iterator<char>(f)), {});
 assert(valid_config_text(provided));
 assert(valid_config_text(""));
 assert(valid_config_text("[CHAMS]\nEnabled=1\nGlowAlpha=.55\nGlowOpenColor=1, .85, .15\nFutureSetting=anything\n"));
 assert(valid_config_text("\xef\xbb\xbf[CHAMS]\r\nEnabled=True\r\n"));
 assert(!valid_config_text("[CHAMS\nEnabled=1\n"));
 assert(!valid_config_text("[CHAMS]\nEnabled=wat\n"));
 assert(!valid_config_text("[CHAMS]\nGlowAlpha=nan\n"));
 assert(!valid_config_text("[CHAMS]\nGlowOpenColor=1,2\n"));
 assert(!valid_config_text("[CHAMS]\nWireSize=123456789012345678901234\n"));
 assert(!valid_config_text("[CHAMS]\nEnabled=1\nEnabled=0\n"));
 assert(!valid_config_text(std::string("[CHAMS]\nEnabled=1\0BAD",22)));
}
