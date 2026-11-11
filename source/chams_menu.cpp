/*
 * chams_menu.cpp -- the Chams page that replaces ReShade's menu content
 * created by hazu@alexainc
 *
 * ReShade's overlay, fonts, style and input are kept; only the page content is
 * ours. The page is deliberately flat - master switch, glow, stroke - because
 * character detection is locked on and everything else is automatic.
 *
 * Plain string literals on purpose: Release builds define RESHADE_LOCALIZATION,
 * where _("x") becomes a CRC16 lookup in ReShade's resource table and would
 * render empty for strings we added.
 */

#if RESHADE_GUI

#include "runtime.hpp"
#include <Windows.h>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <sstream>
#include <locale>
#include "chams.hpp"
#include "chams_capture.hpp"
#include <imgui.h>
#include <fstream>
#include <vector>
#include <string>

static void capture_settings_snapshot(const reshade::chams::settings &s, const char *phase)
{
	try {
		auto valid = [](float x) { return std::isfinite(x) ? x : -1.0f; };
		std::ostringstream row; row.imbue(std::locale::classic()); row.precision(9);
		row << "\"phase\":\"" << phase << "\",\"enabled\":" << s.enabled << ",\"detect\":" << s.detect_mode
		    << ",\"renderer\":" << s.render_mode << ",\"hide_own\":" << s.hide_own << ",\"own_cap\":" << valid(s.own_distance)
		    << ",\"through_walls\":" << s.through_walls << ",\"glow\":" << s.glow << ",\"glow_mode\":" << s.glow_mode
		    << ",\"glow_open\":" << s.glow_open << ",\"glow_alpha\":" << valid(s.glow_alpha);
		auto color = [&](const char *key, const float (&v)[3]) { row << ",\"" << key << "\":[" << valid(v[0]) << ',' << valid(v[1]) << ',' << valid(v[2]) << ']'; };
		color("glow_open_color", s.glow_open_color); color("glow_through_color", s.glow_through_color);
		row << ",\"stroke\":" << s.stroke << ",\"stroke_open\":" << s.stroke_open << ",\"stroke_alpha\":" << valid(s.stroke_alpha)
		    << ",\"stroke_open_size\":" << s.stroke_open_size << ",\"stroke_through_size\":" << s.stroke_through_size;
		color("stroke_open_color", s.stroke_open_color); color("stroke_through_color", s.stroke_through_color);
		row << ",\"wire\":" << s.wire << ",\"wire_open\":" << s.wire_open << ",\"wire_alpha\":" << valid(s.wire_alpha) << ",\"wire_size\":" << s.wire_size;
		color("wire_open_color", s.wire_open_color); color("wire_through_color", s.wire_through_color);
		reshade::chams::capture::runtime_event(0, "settings", row.str());
	} catch (...) { reshade::chams::capture::runtime_record_loss(); }
}

static void save_settings_with_capture(const reshade::chams::settings &s)
{
	reshade::chams::save();
	if (reshade::chams::capture::runtime_enabled()) capture_settings_snapshot(s, "change");
}

void reshade::runtime::draw_gui_chams()
{
	chams::ensure_loaded();

	chams::settings &s = chams::g_settings;

	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);

	// A diagnostic session is deliberately not a persisted Chams setting.
	if (ImGui::CollapsingHeader("Runtime capture (experimental)", ImGuiTreeNodeFlags_DefaultOpen))
	{
		using namespace chams::capture;
		static bool start_failed = false;
		if (!runtime_enabled())
		{
			if (ImGui::Button("Start tracking##capture"))
			{
				std::error_code ec;
				const auto temp = std::filesystem::temp_directory_path(ec);
				try { start_failed = ec || !runtime_start(temp / "ReShade-Chams-Captures"); }
				catch (...) { start_failed = true; }
				if (!start_failed) capture_settings_snapshot(s, "start");
			}
		}
		else if (ImGui::Button("Stop tracking##capture")) { capture_settings_snapshot(s, "stop"); runtime_stop(); }
		if (runtime_enabled()) { ImGui::SameLine();
			if (ImGui::Button("Add marker##capture")) runtime_marker(native_context(), native_surface(), "menu"); }
		const auto capture_status = runtime_status();
		ImGui::SameLine(); ImGui::TextUnformatted(capture_status.active ? "TRACKING ACTIVE" : "Tracking stopped");
		ImGui::Text("Frames: %llu  Contexts: %llu  Draw records: %llu",
			static_cast<unsigned long long>(capture_status.frames), static_cast<unsigned long long>(capture_status.contexts),
			static_cast<unsigned long long>(capture_status.draws));
		ImGui::Text("Dropped: %llu  Incomplete frames: %llu  Writer error: %s", static_cast<unsigned long long>(capture_status.dropped), static_cast<unsigned long long>(capture_status.partial_frames), capture_status.io_error ? "YES" : "no");
		const double seconds = static_cast<double>(capture_status.elapsed_ns) * 1e-9;
		const double mib = static_cast<double>(capture_status.bytes) / (1024.0 * 1024.0);
		ImGui::Text("Elapsed: %.1f s  Written: %.1f MiB  Rate: %.2f MiB/s", seconds, mib, seconds > 0 ? mib / seconds : 0.0);
		ImGui::TextWrapped("F9 marks an event in the trace without consuming the key. Capture may use many gigabytes.");
		const auto capture_path = runtime_directory().u8string();
		if (!capture_path.empty()) { ImGui::TextWrapped("Session: %s", capture_path.c_str());
			if (ImGui::Button("Copy capture folder##capture")) ImGui::SetClipboardText(capture_path.c_str()); }
		if (start_failed) ImGui::TextWrapped("Unable to start capture: check TEMP folder and disk permissions.");
		ImGui::TextWrapped("Experimental capture: coverage and performance have not been verified. Stop flushes the writer.");
	}
	ImGui::Separator();

	// ============================== master switch ==============================
	if (ImGui::Checkbox("Master switch##chams", &s.enabled))
		save_settings_with_capture(s);
	ImGui::SameLine();
	ImGui::TextDisabled("%s  (F8 toggles in game)", s.enabled ? "on" : "off");

	// ---- renderer ------------------------------------------------------------
	ImGui::Spacing();
	ImGui::TextDisabled("renderer:");
	if (ImGui::Combo("##renderer", &s.render_mode,
		"Overlay - mask framebuffer, painted on top of the finished frame (glue wall safe)\0"
		"Direct - per-draw passes, depth tested by the game (safe default)\0"))
		save_settings_with_capture(s);
	ImGui::TextDisabled("overlay: captured into a separate buffer and painted on top of the frame,");
	ImGui::TextDisabled("         so nothing in the scene can hide them. Glue walls, wall skins,");
	ImGui::TextDisabled("         containers - none of them can block the overlay. If the screen goes");
	ImGui::TextDisabled("         black, switch to Direct in this menu or press F8 to toggle chams off/on");

	// ---- detection method ----------------------------------------------------
	ImGui::Spacing();
	ImGui::TextDisabled("character detection:");
	if (ImGui::Combo("##detect", &s.detect_mode,
		"Strict - _CharaLightIntensity (original DLL rule)\0"
		"Wide - any avatar-only uniform (all skins / cloth / hair) [default]\0"
		"Skinned - any bone matrix uniform (unknown skins)\0"
		"All - union of the three\0"))
		save_settings_with_capture(s);
	ImGui::TextDisabled("wide checks 8 avatar-only uniforms taken from the real game files,");
	ImGui::TextDisabled("so new skins / cloth shaders are picked up without an update");
	ImGui::TextDisabled("if something is still missed, try 'Skinned' or 'All'");
	ImGui::TextDisabled("(the game files do not tag the local player, so this is the only discriminator)");

	// ---- through walls: ONE switch, no percentage any more ------------------
	if (ImGui::Checkbox("See enemies through walls and the glue / ice wall##through", &s.through_walls))
		save_settings_with_capture(s);
	ImGui::TextDisabled("on (default) = the wall is drawn after the players, so the chams are pushed");
	ImGui::TextDisabled("               nearer in depth and the wall cannot paint over them");
	ImGui::TextDisabled("off          = only the part behind a car / wall / body is claimed");
	ImGui::TextDisabled("if a weapon or a cloth piece ever disappears, turn this OFF first");

	ImGui::Spacing();
	ImGui::Separator();

	// ======================= third person / my model ==========================
	if (ImGui::CollapsingHeader("Third person (my own character)", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::Checkbox("No chams on my own character (auto)##hideown", &s.hide_own))
			save_settings_with_capture(s);
		if (s.hide_own)
		{
			ImGui::TextDisabled("detected automatically: my model is the CLOSEST draw and its distance");
			ImGui::TextDisabled("stays the same every frame (2.4-2.6 m), enemies keep changing");
			ImGui::TextDisabled("the game does not tag the local player in any shader, so this is geometry only");
			if (ImGui::SliderFloat("Safety cap: my character is nearer than", &s.own_distance, 0.5f, 15.0f, "%.1f m"))
				save_settings_with_capture(s);
			ImGui::TextDisabled("a draw further than this is never treated as mine (protects close enemies)");
		}

		ImGui::Spacing();
		if (ImGui::Checkbox("Also paint hair / cloth shaders (wide match)##wide", &s.wide_match))
			save_settings_with_capture(s);
		ImGui::TextDisabled("those shaders do not declare _CharaLightIntensity; they are matched by _BackLightDir");

		ImGui::Spacing();
		if (ImGui::Checkbox("Dump mode (write draw info to ReShade.log)##dump", &s.dump))
			save_settings_with_capture(s);
		if (s.dump)
		{
			ImGui::TextWrapped("%s",
				"While this is on, every character draw is logged as [CHAMS-DUMP] with "
				"program id, index count, distance, world/camera position and the shader "
				"uniform names. Send that part of ReShade.log to have the own-character "
				"filter made exact instead of distance based.");
			ImGui::TextWrapped("%s %s", "file:", chams::dump_path());
			if (ImGui::Button("Copy dump file path##copydump"))
				ImGui::SetClipboardText(chams::dump_path());
			ImGui::TextDisabled("2000 lines per activation; the file is created when the first line is written");

			ImGui::Spacing();
			if (ImGui::Checkbox("Dump ALL draws (find missing pieces)##dumpall", &s.dump_all))
				save_settings_with_capture(s);
			if (s.dump_all)
			{
				ImGui::TextWrapped("%s",
					"Logs up to 3000 eligible wrapped draw calls plus bounded per-frame "
					"decision counts (including budget skips). It cannot identify objects, "
					"and logging may affect performance. Turn it off after capturing.");
			}
		}
	}

	ImGui::Separator();

	// ================== glow (highlight, full character) ======================
	if (ImGui::CollapsingHeader("Glow (highlight full character)", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::Checkbox("Player highlight##glow", &s.glow))
			save_settings_with_capture(s);
		ImGui::SameLine();
		ImGui::TextDisabled("%s", s.glow ? "on" : "off");

		// ---- what the highlight does to the character -----------------------
		ImGui::Spacing();
		ImGui::TextDisabled("what to show:");
		bool mode_changed = false;
		mode_changed |= ImGui::RadioButton("Solid colour (whole body filled)##gm0", &s.glow_mode, 0);
		mode_changed |= ImGui::RadioButton("Blend (real clothes + colour over them)##gm1", &s.glow_mode, 1);
		mode_changed |= ImGui::RadioButton("Real clothes + light colour##gm2", &s.glow_mode, 2);
		mode_changed |= ImGui::RadioButton("Real clothes only (no colour)##gm3", &s.glow_mode, 3);
		if (mode_changed)
			save_settings_with_capture(s);
		ImGui::TextDisabled("solid = classic chams | blend = texture still readable");
		ImGui::TextDisabled("real clothes = the actual outfit / skin / hair / gun is drawn, also through walls");
		ImGui::TextDisabled("if the colours still hide the clothes, pick one of the two 'real clothes' modes");

		if (s.glow_mode == 0 || s.glow_mode == 1)
		{
			ImGui::TextDisabled("(the colour and strength below are used by this mode)");
		}
		else
		{
			ImGui::TextDisabled("(this mode shows the real clothes; strength only adds a light tint)");
		}

		if (ImGui::SliderFloat("Glow strength", &s.glow_alpha, 0.05f, 1.0f, "%.2f"))
			save_settings_with_capture(s);

		ImGui::Spacing();
		if (ImGui::ColorEdit3("Through walls - colour", s.glow_through_color, ImGuiColorEditFlags_NoInputs))
			save_settings_with_capture(s);
		ImGui::TextDisabled("part hidden behind a wall, a car or the throwable glue wall");

		ImGui::Spacing();
		if (ImGui::Checkbox("Open part (not covered)##glowopen", &s.glow_open))
			save_settings_with_capture(s);
		if (s.glow_open)
		{
			if (ImGui::ColorEdit3("Open part - colour", s.glow_open_color, ImGuiColorEditFlags_NoInputs))
				save_settings_with_capture(s);
		}
		else
		{
			ImGui::TextDisabled("open part shows its own colour only when this is on");
		}
	}

	ImGui::Separator();

	// ================== stroke: ONE 2D outline, one colour =====================
	if (ImGui::CollapsingHeader("Stroke (2D outline - the drawn line)", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::Checkbox("Stroke##stroke", &s.stroke))
			save_settings_with_capture(s);
		ImGui::SameLine();
		ImGui::TextDisabled("%s", s.stroke ? "on" : "off");

		if (ImGui::SliderInt("Stroke size - open area (px)##strokesizeopen", &s.stroke_open_size, 1, 16))
			save_settings_with_capture(s);
		if (ImGui::SliderInt("Stroke size - covered area (px)##strokesizethrough", &s.stroke_through_size, 1, 16))
			save_settings_with_capture(s);
		ImGui::TextDisabled("open = plain sight | covered = behind a wall, glue wall, car or another body");
		ImGui::TextDisabled("each width is saved independently and applied in real time.");
		ImGui::TextDisabled("thickness of the 2D outline in pixels; it grows with the character on screen, never below 2 px");

		if (ImGui::SliderFloat("Stroke strength", &s.stroke_alpha, 0.05f, 1.0f, "%.2f"))
			save_settings_with_capture(s);

		ImGui::Spacing();
		ImGui::Spacing();
		if (ImGui::Checkbox("Open part (not covered)##strokeopen", &s.stroke_open))
			save_settings_with_capture(s);
		if (s.stroke_open)
		{
			if (ImGui::ColorEdit3("Open part - colour##stopen", s.stroke_open_color, ImGuiColorEditFlags_NoInputs))
				save_settings_with_capture(s);
		}
		ImGui::TextDisabled("the line you see around the character in plain sight");

		ImGui::Spacing();
		if (ImGui::ColorEdit3("Through walls - colour##stcolour", s.stroke_through_color, ImGuiColorEditFlags_NoInputs))
			save_settings_with_capture(s);
		ImGui::TextDisabled("the line where a wall, a car or another body is in front");
		ImGui::TextDisabled("both use the v2.3 mask+ring method, so the body is never filled");

		ImGui::Spacing();
		ImGui::Separator();

		// ---------------- 3D wireframe stroke (its own switch) ----------------
		if (ImGui::Checkbox("3D wireframe stroke##wire", &s.wire))
			save_settings_with_capture(s);
		ImGui::SameLine();
		ImGui::TextDisabled("%s", s.wire ? "on" : "off");

		if (s.wire)
		{
			if (ImGui::SliderInt("Wire size (px)##wiresize", &s.wire_size, 1, 8))
				save_settings_with_capture(s);
			if (ImGui::SliderFloat("Wire strength", &s.wire_alpha, 0.05f, 1.0f, "%.2f"))
				save_settings_with_capture(s);

			if (ImGui::ColorEdit3("Wire through walls - colour", s.wire_through_color, ImGuiColorEditFlags_NoInputs))
				save_settings_with_capture(s);

			if (ImGui::Checkbox("Wire open part (not covered)##wireopen", &s.wire_open))
				save_settings_with_capture(s);
			if (s.wire_open)
			{
				if (ImGui::ColorEdit3("Wire open part - colour", s.wire_open_color, ImGuiColorEditFlags_NoInputs))
					save_settings_with_capture(s);
			}
			ImGui::TextDisabled("triangles of the body drawn as lines - independent of the 2D outline");
		}
	}

	ImGui::Separator();

	// ======================= logs tab =======================
	if (ImGui::CollapsingHeader("Chams logs (dump file)", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::TextDisabled("Recent lines from %%TEMP%%\\chams-dump.txt:");
		ImGui::SameLine();
		ImGui::TextDisabled("%s", chams::dump_path());

		static std::vector<std::string> s_log_lines;
		static bool s_logs_loaded = false;
		if (!s_logs_loaded)
		{
			std::ifstream logf(chams::dump_path(), std::ios::in);
			if (logf.is_open())
			{
				std::string line;
				while (std::getline(logf, line))
					s_log_lines.push_back(line);
				logf.close();
			}
			s_logs_loaded = true;
		}

		if (ImGui::Button("Refresh logs##chamslogs"))
		{
			s_log_lines.clear();
			s_logs_loaded = false;
		}

		ImGui::SameLine();
		if (ImGui::Button("Open log folder##chamslogsfolder"))
		{
			char path[MAX_PATH * 4] = {};
			GetTempPathA(sizeof(path), path);
			std::string cmd = "explorer \"";
			cmd += path;
			cmd += "\"";
			system(cmd.c_str());
		}

		// Show last 20 lines
		int start = (int)s_log_lines.size();
		if (start > 20) start -= 20;
		if (start < 0) start = 0;

		ImGui::BeginChild("chamslogscroll", ImVec2(0, 120), true);
		for (int i = start; i < (int)s_log_lines.size(); ++i)
		{
			const std::string &line = s_log_lines[i];
			ImVec4 col = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
			if (line.find("[CHAMS]") != std::string::npos) col = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
			else if (line.find("error") != std::string::npos || line.find("failed") != std::string::npos) col = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
			else if (line.find("warning") != std::string::npos) col = ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
			ImGui::TextColored(col, "%s", line.c_str());
		}
		ImGui::EndChild();

		ImGui::TextDisabled("Logs are also written to ReShade.log (next to HD-Player.exe).");
		ImGui::TextDisabled("2000 lines per dump activation; file is created on first write.");
	}

	if (ImGui::Button("Reset"))
	{
		s = chams::settings();
		save_settings_with_capture(s);
	}

	ImGui::SameLine();
	ImGui::TextDisabled("%s", "created by hazu@alexainc");

	ImGui::PopItemWidth();
}

#endif // RESHADE_GUI
