/*
 * chams.cpp -- persistence + hotkey + dump file + per-frame bookkeeping.
 * created by hazu@alexainc
 */

#include "chams.hpp"
#include "ini_file.hpp"
#include "dll_log.hpp"

#include <Windows.h>   // GetAsyncKeyState, CreateFileW, GetTempPathW, SHGetFolderPath
#include <Shlobj.h>    // SHGetFolderPath, CSIDL_APPDATA
#include <algorithm>
#include <cstring>
#include <cwchar>     // wcscat_s

namespace reshade::chams
{
	settings g_settings;
	bool     g_overlay_open = false;
	int      g_budget_left  = k_max_draws_per_frame;
	unsigned g_frame_id     = 0;

	// ======================= dump file ==================================
	// ReShade.log sits next to HD-Player.exe, which is often read-only (the
	// emulator folder lives under Program Files / is protected). The dump is
	// therefore written to the user's TEMP folder instead:
	//     %TEMP%\\chams-dump.txt      (usually C:\\Users\\<you>\\AppData\\Local\\Temp)
	// If the emulator runs elevated, that is the TEMP of the elevated user or
	// C:\\Windows\\Temp - the exact path is printed into ReShade.log on start,
	// and also shown in the menu.
	static HANDLE          s_dump_file = INVALID_HANDLE_VALUE;
	static char            s_dump_path_utf8[MAX_PATH * 4] = {};
	static unsigned        s_dump_lines = 0;

	// ======================= chams config path ==========================
	// Settings are saved to %APPDATA%\\reshade\\chams\\chams.ini so they
	// survive emulator reinstalls and are not blocked by read-only game
	// folders (Program Files, etc.).
	static std::wstring s_chams_config_path;

	static void build_chams_config_path()
	{
		if (!s_chams_config_path.empty())
			return;

		wchar_t appdata[MAX_PATH] = {};
		if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)))
		{
			s_chams_config_path = appdata;
		}
		else
		{
			// fallback: use the TEMP path
			DWORD len = GetTempPathW(MAX_PATH, appdata);
			if (len == 0 || len >= MAX_PATH)
				return;
			s_chams_config_path = appdata;
		}

		if (s_chams_config_path.back() != L'\\')
			s_chams_config_path += L'\\';
		s_chams_config_path += L"reshade\\chams\\chams.ini";

		// create the directory (reshade/chams) if it does not exist
		std::wstring dir = s_chams_config_path;
		dir = dir.substr(0, dir.find_last_of(L'\\'));
		CreateDirectoryW(dir.c_str(), nullptr);
	}

	static ini_file &chams_config()
	{
		build_chams_config_path();
		return ini_file::load_cache(std::filesystem::path(s_chams_config_path));
	}

	static void build_dump_path(WCHAR (&out)[MAX_PATH])
	{
		DWORD len = GetTempPathW(MAX_PATH, out);
		if (len == 0 || len >= MAX_PATH)
		{
			out[0] = L'\\0';
			return;
		}
		if (out[len - 1] != L'\\')
		{
			out[len]     = L'\\';
			out[len + 1] = L'\\0';
		}
		wcscat_s(out, MAX_PATH, L"chams-dump.txt");
	}

	static void open_dump_file()
	{
		if (s_dump_file != INVALID_HANDLE_VALUE)
			return;

		WCHAR wide[MAX_PATH] = {};
		build_dump_path(wide);
		if (wide[0] == L'\\0')
			return;

		// one file per game start: truncate here, append afterwards
		s_dump_file = CreateFileW(wide, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
		                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
		if (s_dump_file == INVALID_HANDLE_VALUE)
			return;

		// remember the path as UTF-8 for the menu / log
		const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, s_dump_path_utf8,
		                                  sizeof(s_dump_path_utf8), nullptr, nullptr);
		if (n <= 0)
			s_dump_path_utf8[0] = '\\0';

		reshade::log::message(reshade::log::level::info,
		                      "[CHAMS] dump file: %s", s_dump_path_utf8);
	}

	void dump_line(const char *text)
	{
		open_dump_file();

		if (s_dump_file == INVALID_HANDLE_VALUE)
		{
			// last resort: at least put it in ReShade.log
			reshade::log::message(reshade::log::level::info, "%s", text);
			return;
		}

		DWORD written = 0;
		WriteFile(s_dump_file, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
		WriteFile(s_dump_file, "\\r\\n", 2, &written, nullptr);
		++s_dump_lines;
	}

	const char *dump_path()
	{
		return s_dump_path_utf8[0] != '\\0' ? s_dump_path_utf8 : "(not created yet)";
	}

	void begin_frame()
	{
		g_budget_left = k_max_draws_per_frame;
		++g_frame_id;
	}

	void poll_hotkey()
	{
		static bool was_down = false;
		const bool down = (GetAsyncKeyState(k_toggle_key) & 0x8000) != 0;

		if (down && !was_down)
		{
			g_settings.enabled = !g_settings.enabled;
			reshade::log::message(reshade::log::level::info,
			                      "[CHAMS] toggled by hotkey -> %s", g_settings.enabled ? "on" : "off");
		}
		was_down = down;
	}

	static void clamp_colour(float c[3])
	{
		for (int i = 0; i < 3; ++i)
			c[i] = std::clamp(c[i], 0.0f, 1.0f);
	}

	static void clamp_settings()
	{
		clamp_colour(g_settings.glow_through_color);
		clamp_colour(g_settings.glow_open_color);
		clamp_colour(g_settings.stroke_through_color);
		clamp_colour(g_settings.stroke_open_color);
		clamp_colour(g_settings.wire_through_color);
		clamp_colour(g_settings.wire_open_color);

		g_settings.glow_alpha   = std::clamp(g_settings.glow_alpha, 0.05f, 1.0f);
		g_settings.stroke_alpha     = std::clamp(g_settings.stroke_alpha, 0.05f, 1.0f);
		g_settings.stroke_open_size  = std::clamp(g_settings.stroke_open_size, 1, 16);
		g_settings.stroke_through_size = std::clamp(g_settings.stroke_through_size, 1, 16);
		g_settings.wire_alpha   = std::clamp(g_settings.wire_alpha, 0.05f, 1.0f);
		g_settings.wire_size    = std::clamp(g_settings.wire_size, 1, 8);
		g_settings.own_distance = std::clamp(g_settings.own_distance, 0.5f, 15.0f);
		g_settings.glow_mode    = std::clamp(g_settings.glow_mode, 0, 3);
		g_settings.render_mode  = std::clamp(g_settings.render_mode, 0, 1);
		g_settings.detect_mode  = std::clamp(g_settings.detect_mode, 0, 3);
	}

	void load()
	{
		ini_file &config = chams_config();

		config.get("CHAMS", "Enabled",            g_settings.enabled);

		config.get("CHAMS", "Glow",               g_settings.glow);
		config.get("CHAMS", "GlowMode",           g_settings.glow_mode);
		config.get("CHAMS", "RenderMode",         g_settings.render_mode);
		config.get("CHAMS", "DetectMode",         g_settings.detect_mode);
		config.get("CHAMS", "GlowThroughColor",   g_settings.glow_through_color);
		config.get("CHAMS", "GlowOpen",           g_settings.glow_open);
		config.get("CHAMS", "GlowOpenColor",      g_settings.glow_open_color);
		config.get("CHAMS", "GlowAlpha",          g_settings.glow_alpha);

		config.get("CHAMS", "Stroke",             g_settings.stroke);
		config.get("CHAMS", "StrokeSizeOpen",     g_settings.stroke_open_size);
		config.get("CHAMS", "StrokeSizeThrough",  g_settings.stroke_through_size);
		config.get("CHAMS", "StrokeThroughColor", g_settings.stroke_through_color);
		config.get("CHAMS", "StrokeOpen",         g_settings.stroke_open);
		config.get("CHAMS", "StrokeOpenColor",    g_settings.stroke_open_color);
		config.get("CHAMS", "StrokeAlpha",        g_settings.stroke_alpha);

		config.get("CHAMS", "Wire",               g_settings.wire);
		config.get("CHAMS", "WireSize",           g_settings.wire_size);
		config.get("CHAMS", "WireAlpha",          g_settings.wire_alpha);
		config.get("CHAMS", "WireThroughColor",   g_settings.wire_through_color);
		config.get("CHAMS", "WireOpen",           g_settings.wire_open);
		config.get("CHAMS", "WireOpenColor",      g_settings.wire_open_color);

		config.get("CHAMS", "HideOwn",            g_settings.hide_own);
		config.get("CHAMS", "OwnDistance",        g_settings.own_distance);
		config.get("CHAMS", "Dump",               g_settings.dump);
		config.get("CHAMS", "DumpAll",            g_settings.dump_all);
		config.get("CHAMS", "ThroughWalls",       g_settings.through_walls);
		config.get("CHAMS", "WideMatch",          g_settings.wide_match);

		// migration: old single StrokeSize key -> the two independent widths
		int stroke_size_legacy = 0;
		if (config.get("CHAMS", "StrokeSize", stroke_size_legacy))
		{
			if (!config.get("CHAMS", "StrokeSizeOpen", g_settings.stroke_open_size))
				g_settings.stroke_open_size = stroke_size_legacy;
			if (!config.get("CHAMS", "StrokeSizeThrough", g_settings.stroke_through_size))
				g_settings.stroke_through_size = stroke_size_legacy;
		}

		clamp_settings();
	}

	void save()
	{
		clamp_settings();

		ini_file &config = chams_config();

		config.set("CHAMS", "Enabled",            g_settings.enabled);

		config.set("CHAMS", "Glow",               g_settings.glow);
		config.set("CHAMS", "GlowMode",           g_settings.glow_mode);
		config.set("CHAMS", "RenderMode",         g_settings.render_mode);
		config.set("CHAMS", "DetectMode",         g_settings.detect_mode);
		config.set("CHAMS", "GlowThroughColor",   g_settings.glow_through_color);
		config.set("CHAMS", "GlowOpen",           g_settings.glow_open);
		config.set("CHAMS", "GlowOpenColor",      g_settings.glow_open_color);
		config.set("CHAMS", "GlowAlpha",          g_settings.glow_alpha);

		config.set("CHAMS", "Stroke",             g_settings.stroke);
		config.set("CHAMS", "StrokeSizeOpen",     g_settings.stroke_open_size);
		config.set("CHAMS", "StrokeSizeThrough",  g_settings.stroke_through_size);
		config.set("CHAMS", "StrokeThroughColor", g_settings.stroke_through_color);
		config.set("CHAMS", "StrokeOpen",         g_settings.stroke_open);
		config.set("CHAMS", "StrokeOpenColor",    g_settings.stroke_open_color);
		config.set("CHAMS", "StrokeAlpha",        g_settings.stroke_alpha);

		config.set("CHAMS", "Wire",               g_settings.wire);
		config.set("CHAMS", "WireSize",           g_settings.wire_size);
		config.set("CHAMS", "WireAlpha",          g_settings.wire_alpha);
		config.set("CHAMS", "WireThroughColor",   g_settings.wire_through_color);
		config.set("CHAMS", "WireOpen",           g_settings.wire_open);
		config.set("CHAMS", "WireOpenColor",      g_settings.wire_open_color);

		config.set("CHAMS", "HideOwn",            g_settings.hide_own);
		config.set("CHAMS", "OwnDistance",        g_settings.own_distance);
		config.set("CHAMS", "Dump",               g_settings.dump);
		config.set("CHAMS", "DumpAll",            g_settings.dump_all);
		config.set("CHAMS", "ThroughWalls",       g_settings.through_walls);
		config.set("CHAMS", "WideMatch",          g_settings.wide_match);

		config.save();
	}
}
