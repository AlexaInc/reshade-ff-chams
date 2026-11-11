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
#include "chams.hpp"
#include <imgui.h>
#include <fstream>
#include <vector>
#include <string>

void reshade::runtime::draw_gui_chams()
{
	chams::ensure_loaded();

	chams::settings &s = chams::g_settings;

	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);

	// ============================== master switch ==============================
	if (ImGui::Checkbox("Master switch##chams", &s.enabled))
		chams::save();
	ImGui::SameLine();
	ImGui::TextDisabled("%s  (F8 toggles in game)", s.enabled ? "on" : "off");

	// ---- renderer ------------------------------------------------------------
	ImGui::Spacing();
	ImGui::TextDisabled("renderer:");
	if (ImGui::Combo("##renderer", &s.render_mode,
		"Overlay - mask framebuffer, painted on top of the finished frame (glue wall safe)\0"
		"Direct - per-draw passes, depth tested by the game (safe default)\0"))
		chams::save();
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
		chams::save();
	ImGui::TextDisabled("wide checks 8 avatar-only uniforms taken from the real game files,");
	ImGui::TextDisabled("so new skins / cloth shaders are picked up without an update");
	ImGui::TextDisabled("if something is still missed, try 'Skinned' or 'All'");
	ImGui::TextDisabled("(the game files do not tag the local player, so this is the only discriminator)");

	// ---- through walls: ONE switch, no percentage any more ------------------
	if (ImGui::Checkbox("See enemies through walls and the glue / ice wall##through", &s.through_walls))
		chams::save();
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
			chams::save();
		if (s.hide_own)
		{
			ImGui::TextDisabled("detected automatically: my model is the CLOSEST draw and its distance");
			ImGui::TextDisabled("stays the same every frame (2.4-2.6 m), enemies keep changing");
			ImGui::TextDisabled("the game does not tag the local player in any shader, so this is geometry only");
			if (ImGui::SliderFloat("Safety cap: my character is nearer than", &s.own_distance, 0.5f, 15.0f, "%.1f m"))
				chams::save();
			ImGui::TextDisabled("a draw further than this is never treated as mine (protects close enemies)");
		}

		ImGui::Spacing();
		if (ImGui::Checkbox("Also paint hair / cloth shaders (wide match)##wide", &s.wide_match))
			chams::save();
		ImGui::TextDisabled("those shaders do not declare _CharaLightIntensity; they are matched by _BackLightDir");

		ImGui::Spacing();
		if (ImGui::Checkbox("Dump mode (write draw info to ReShade.log)##dump", &s.dump))
			chams::save();
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
				chams::save();
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
			chams::save();
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
			chams::save();
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
			chams::save();

		ImGui::Spacing();
		if (ImGui::ColorEdit3("Through walls - colour", s.glow_through_color, ImGuiColorEditFlags_NoInputs))
			chams::save();
		ImGui::TextDisabled("part hidden behind a wall, a car or the throwable glue wall");

		ImGui::Spacing();
		if (ImGui::Checkbox("Open part (not covered)##glowopen", &s.glow_open))
			chams::save();
		if (s.glow_open)
		{
			if (ImGui::ColorEdit3("Open part - colour", s.glow_open_color, ImGuiColorEditFlags_NoInputs))
				chams::save();
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
			chams::save();
		ImGui::SameLine();
		ImGui::TextDisabled("%s", s.stroke ? "on" : "off");

		if (ImGui::SliderInt("Stroke size - open area (px)##strokesizeopen", &s.stroke_open_size, 1, 16))
			chams::save();
		if (ImGui::SliderInt("Stroke size - covered area (px)##strokesizethrough", &s.stroke_through_size, 1, 16))
			chams::save();
		ImGui::TextDisabled("open = plain sight | covered = behind a wall, glue wall, car or another body");
		ImGui::TextDisabled("each width is saved independently and applied in real time.");
		ImGui::TextDisabled("thickness of the 2D outline in pixels; it grows with the character on screen, never below 2 px");

		if (ImGui::SliderFloat("Stroke strength", &s.stroke_alpha, 0.05f, 1.0f, "%.2f"))
			chams::save();

		ImGui::Spacing();
		ImGui::Spacing();
		if (ImGui::Checkbox("Open part (not covered)##strokeopen", &s.stroke_open))
			chams::save();
		if (s.stroke_open)
		{
			if (ImGui::ColorEdit3("Open part - colour##stopen", s.stroke_open_color, ImGuiColorEditFlags_NoInputs))
				chams::save();
		}
		ImGui::TextDisabled("the line you see around the character in plain sight");

		ImGui::Spacing();
		if (ImGui::ColorEdit3("Through walls - colour##stcolour", s.stroke_through_color, ImGuiColorEditFlags_NoInputs))
			chams::save();
		ImGui::TextDisabled("the line where a wall, a car or another body is in front");
		ImGui::TextDisabled("both use the v2.3 mask+ring method, so the body is never filled");

		ImGui::Spacing();
		ImGui::Separator();

		// ---------------- 3D wireframe stroke (its own switch) ----------------
		if (ImGui::Checkbox("3D wireframe stroke##wire", &s.wire))
			chams::save();
		ImGui::SameLine();
		ImGui::TextDisabled("%s", s.wire ? "on" : "off");

		if (s.wire)
		{
			if (ImGui::SliderInt("Wire size (px)##wiresize", &s.wire_size, 1, 8))
				chams::save();
			if (ImGui::SliderFloat("Wire strength", &s.wire_alpha, 0.05f, 1.0f, "%.2f"))
				chams::save();

			if (ImGui::ColorEdit3("Wire through walls - colour", s.wire_through_color, ImGuiColorEditFlags_NoInputs))
				chams::save();

			if (ImGui::Checkbox("Wire open part (not covered)##wireopen", &s.wire_open))
				chams::save();
			if (s.wire_open)
			{
				if (ImGui::ColorEdit3("Wire open part - colour", s.wire_open_color, ImGuiColorEditFlags_NoInputs))
					chams::save();
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
		chams::save();
	}

	ImGui::SameLine();
	ImGui::TextDisabled("%s", "created by hazu@alexainc");

	ImGui::PopItemWidth();
}

#endif // RESHADE_GUI
