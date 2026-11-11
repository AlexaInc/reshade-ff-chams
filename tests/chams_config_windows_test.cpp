// Windows integration test of the actual Chams/ini_file persistence code.
#include <Windows.h>
#include <Shlobj.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cassert>
#include <string>
#include <thread>
#include <vector>
#include <unordered_set>

static std::wstring test_appdata;
static HRESULT WINAPI test_appdata_path(HWND, int, HANDLE, DWORD, LPWSTR out)
{
 if (test_appdata.size() >= MAX_PATH) return E_FAIL;
 wcscpy_s(out, MAX_PATH, test_appdata.c_str());
 return S_OK;
}
static DWORD WINAPI test_temp_path(DWORD size, LPWSTR out)
{
 const std::wstring path = test_appdata + L"\\";
 if (size <= path.size()) return static_cast<DWORD>(path.size() + 1);
 wcscpy_s(out, size, path.c_str());
 return static_cast<DWORD>(path.size());
}
#define SHGetFolderPathW test_appdata_path
#define GetTempPathW test_temp_path
#include "../source/chams.cpp"
#undef GetTempPathW
#undef SHGetFolderPathW
namespace reshade::log { void message(level, const char *, ...) {} }
std::filesystem::path g_reshade_dll_path, g_reshade_base_path, g_target_executable_path;

static std::string read_all(const std::filesystem::path &p)
{
 std::ifstream in(p, std::ios::binary);
 return std::string((std::istreambuf_iterator<char>(in)), {});
}
static void reset_config()
{
 reshade::ini_file::clear_cache();
 reshade::chams::s_chams_config_path.clear();
 reshade::chams::s_settings_loaded = false;
 reshade::chams::g_settings = reshade::chams::settings();
}
int main()
{
 namespace fs = std::filesystem;
 namespace c = reshade::chams;
 const fs::path base = fs::temp_directory_path() / (L"chams-config-test-" + std::to_wstring(GetCurrentProcessId()));
 fs::remove_all(base);
 fs::create_directories(base);
 test_appdata = base.wstring();
 const fs::path ini = base / L"reshade" / L"chams" / L"chams.ini";

 // First initialization creates parents but does not overwrite/create a file
 // until the user saves; then actual source save writes the setting.
 c::ensure_loaded();
 assert(fs::is_directory(ini.parent_path()) && !fs::exists(ini));
 c::g_settings.glow_alpha = .73f;
 c::g_settings.detect_mode = 0;
 c::save();
 assert(fs::exists(ini));
 assert(read_all(ini).find("DetectMode=0") != std::string::npos);
 const std::string saved = read_all(ini);

 // Simulated next process: load existing settings without opening the menu.
 reset_config(); c::load();
 assert(c::s_settings_loaded && c::g_settings.detect_mode == 0);
 assert(c::g_settings.glow_alpha > .729f && c::g_settings.glow_alpha < .731f);
 assert(read_all(ini) == saved); // loading never writes defaults

 // Invalid and unreadable data are not replaced with default settings.
 reshade::ini_file::clear_cache();
 { std::ofstream out(ini, std::ios::binary | std::ios::trunc); out << "[CHAMS]\nGlowAlpha=nan\n"; }
 const std::string bad = read_all(ini);
 reset_config(); c::load(); assert(!c::s_settings_loaded); c::save();
 assert(read_all(ini) == bad);

 // An empty but readable file is accepted and left untouched by loading.
 { std::ofstream out(ini, std::ios::binary | std::ios::trunc); }
 reset_config(); c::load(); assert(c::s_settings_loaded && read_all(ini).empty());
 c::save(); assert(!read_all(ini).empty());

 // A file held exclusively by another process is not truncated on save.
 const std::string before_lock = read_all(ini);
 HANDLE locked = CreateFileW(ini.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
 assert(locked != INVALID_HANDLE_VALUE);
 c::g_settings.detect_mode = 3;
 c::save();
 CloseHandle(locked);
 assert(read_all(ini) == before_lock);

 // Real concurrent dump writes must remain complete CRLF-terminated records.
 // GetTempPathW is redirected above so this cannot truncate a user's dump.
 std::vector<std::thread> writers;
 for (int t = 0; t < 4; ++t)
  writers.emplace_back([t] {
   for (int i = 0; i < 100; ++i) {
    const std::string line = "writer=" + std::to_string(t) + ",line=" + std::to_string(i);
    c::dump_line(line.c_str());
   }
  });
 for (auto &writer : writers) writer.join();
 assert(c::s_dump_file != INVALID_HANDLE_VALUE);
 CloseHandle(c::s_dump_file); c::s_dump_file = INVALID_HANDLE_VALUE;
 const std::string log = read_all(base / L"chams-dump.txt");
 std::unordered_set<std::string> actual;
 size_t pos = 0, count = 0;
 while (pos < log.size()) {
  const auto end = log.find("\r\n", pos); assert(end != std::string::npos);
  actual.insert(log.substr(pos, end - pos)); pos = end + 2; ++count;
 }
 assert(count == 400 && actual.size() == 400);
 for (int t = 0; t < 4; ++t)
  for (int i = 0; i < 100; ++i)
   assert(actual.count("writer=" + std::to_string(t) + ",line=" + std::to_string(i)) == 1);
 reshade::ini_file::clear_cache();
 fs::remove_all(base);
}
