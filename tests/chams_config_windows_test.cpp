// Windows integration test of the actual Chams/ini_file persistence code.
#include <Windows.h>
#include <Shlobj.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cassert>
#include <string>

static std::wstring test_appdata;
static HRESULT WINAPI test_appdata_path(HWND, int, HANDLE, DWORD, LPWSTR out)
{
 if (test_appdata.size() >= MAX_PATH) return E_FAIL;
 wcscpy_s(out, MAX_PATH, test_appdata.c_str());
 return S_OK;
}
#define SHGetFolderPathW test_appdata_path
#include "../source/chams.cpp"
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
 reshade::ini_file::clear_cache();
 fs::remove_all(base);
}
