/* Chams INI safety check: refuse to overwrite malformed input. */
#pragma once
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <set>
#include <string>
#include <string_view>

namespace reshade::chams
{
	inline std::string_view trim_config(std::string_view s)
	{
		const size_t a = s.find_first_not_of(" \t\r\n");
		return a == s.npos ? std::string_view{} : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
	}

	inline bool valid_config_number(std::string_view s, bool integer)
	{
		const std::string value(trim_config(s));
		if (value.empty()) return false;
		char *end = nullptr;
		errno = 0;
		if (integer)
		{
			const long number = std::strtol(value.c_str(), &end, 10);
			if (errno == ERANGE || number < -2147483647L - 1L || number > 2147483647L) return false;
		}
		else
		{
			const double number = std::strtod(value.c_str(), &end);
			if (errno == ERANGE || !std::isfinite(number) || std::fabs(number) > 3.402823466e38) return false;
		}
		return end == value.c_str() + value.size();
	}

	inline bool valid_config_value(std::string_view key, std::string_view value)
	{
		static constexpr std::string_view flags[] = { "Enabled", "Glow", "GlowOpen", "Stroke", "StrokeOpen", "Wire", "WireOpen", "HideOwn", "Dump", "DumpAll", "ThroughWalls", "WideMatch" };
		static constexpr std::string_view integers[] = { "GlowMode", "RenderMode", "DetectMode", "StrokeSize", "StrokeSizeOpen", "StrokeSizeThrough", "WireSize" };
		static constexpr std::string_view numbers[] = { "GlowAlpha", "StrokeAlpha", "WireAlpha", "OwnDistance" };
		static constexpr std::string_view colors[] = { "GlowThroughColor", "GlowOpenColor", "StrokeThroughColor", "StrokeOpenColor", "WireThroughColor", "WireOpenColor" };
		value = trim_config(value);
		for (auto name : flags) if (key == name)
			return value == "0" || value == "1" || value == "true" || value == "false" || value == "True" || value == "False" || value == "TRUE" || value == "FALSE";
		for (auto name : integers) if (key == name) return valid_config_number(value, true);
		for (auto name : numbers) if (key == name) return valid_config_number(value, false);
		for (auto name : colors) if (key == name)
		{
			for (int i = 0; i < 3; ++i)
			{
				const size_t sep = value.find(',');
				if ((i < 2 && sep == value.npos) || (i == 2 && sep != value.npos) || !valid_config_number(value.substr(0, sep), false)) return false;
				if (sep != value.npos) value.remove_prefix(sep + 1);
			}
			return true;
		}
		return true; // Preserve unrecognised keys for forward/backward compatibility.
	}

	inline bool valid_config_text(std::string_view text)
	{
		if (text.size() >= 3 && text.substr(0, 3) == "\xef\xbb\xbf") text.remove_prefix(3);
		if (text.find('\0') != text.npos) return false;
		std::string_view section;
		std::set<std::string> chams_keys;
		while (!text.empty())
		{
			const auto eol = text.find('\n');
			const auto line = trim_config(text.substr(0, eol));
			if (!line.empty() && line[0] != ';' && line[0] != '#' && line[0] != '/')
			{
				if (line[0] == '[')
				{
					if (line.size() < 3 || line.back() != ']' || line.find(']') != line.size() - 1) return false;
					section = trim_config(line.substr(1, line.size() - 2));
					if (section.empty()) return false;
				}
				else
				{
					const auto sep = line.find('=');
					if (sep == line.npos || trim_config(line.substr(0, sep)).empty()) return false;
					if (section == "CHAMS")
					{
						const auto key = trim_config(line.substr(0, sep));
						if (!chams_keys.emplace(key).second || !valid_config_value(key, line.substr(sep + 1))) return false;
					}
				}
			}
			if (eol == text.npos) break;
			text.remove_prefix(eol + 1);
		}
		return true;
	}
}
