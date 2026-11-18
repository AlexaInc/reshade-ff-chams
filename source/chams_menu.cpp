/*
 * chams_menu.cpp -- Collapsible Catalog Panel (AlexaInc Graphics Enhancer)
 * designed from Figma: GpQvYBzm8d1wet9mdajwvY node 1005-24
 * Features: numbered catalog panels, AlexaInc header logo, expand/collapse all,
 *           Figma-dark theme with mint accent, fix FPS visibility.
 *
 * created by hazu@alexainc + design catalog retrofit
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
#include <unordered_map>

namespace {

// ---------- Catalog styling (Figma: dark #0F1419 / card #1C232E / mint #7ED3B2) ----------
static const ImVec4 kBg          = ImVec4(0.09f, 0.12f, 0.16f, 1.00f);
static const ImVec4 kCardBg      = ImVec4(0.13f, 0.16f, 0.20f, 1.00f);
static const ImVec4 kCardActive  = ImVec4(0.14f, 0.19f, 0.22f, 1.00f);
static const ImVec4 kMint        = ImVec4(0.49f, 0.84f, 0.70f, 1.00f);
static const ImVec4 kMintDim     = ImVec4(0.14f, 0.30f, 0.24f, 1.00f);
static const ImVec4 kPink        = ImVec4(0.94f, 0.61f, 0.80f, 1.00f);
static const ImVec4 kText        = ImVec4(0.90f, 0.92f, 0.94f, 1.00f);
static const ImVec4 kTextDim     = ImVec4(0.55f, 0.60f, 0.67f, 1.00f);
static const ImVec4 kBorder      = ImVec4(0.20f, 0.26f, 0.32f, 1.00f);

static std::unordered_map<std::string, bool> g_catalog_open = {
	{"master", true}, {"render", true}, {"detect", true}, {"glow", true},
	{"stroke", true}, {"wire", false}, {"own", true}, {"fps", true}, {"logs", false}, {"capture", false}
};

static bool g_expand_all_transition = false;
static char g_search_buf[128] = "";

static std::string ToLowerStr(const std::string &s) {
	std::string r; r.reserve(s.size());
	for (unsigned char c : s) r.push_back((char)std::tolower(c));
	return r;
}
static bool ContainsI(const std::string &hay, const std::string &needle) {
	if (needle.empty()) return true;
	std::string hl = ToLowerStr(hay);
	std::string nl = ToLowerStr(needle);
	return hl.find(nl) != std::string::npos;
}
static bool SectionMatches(const std::string &title, const std::string &subtitle, const std::string &keywords) {
	if (g_search_buf[0]=='\0') return true;
	std::string needle(g_search_buf);
	// match title, subtitle, or keywords blob
	return ContainsI(title, needle) || ContainsI(subtitle, needle) || ContainsI(keywords, needle);
}

// Mint toggle switch matching Figma (track + knob)
bool AlexaToggle(const char* id, bool* v, const char* labelLeft = nullptr) {
	if (labelLeft) {
		ImGui::TextDisabled("%s", labelLeft);
		ImGui::SameLine();
	}
	ImVec2 size(40, 20);
	ImGui::InvisibleButton(id, size);
	bool hovered = ImGui::IsItemHovered();
	bool clicked = ImGui::IsItemClicked();
	if (clicked) *v = !*v;

	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 p0 = ImGui::GetItemRectMin();
	ImVec2 p1 = p0; p1.x += size.x; p1.y += size.y;
	float r = size.y * 0.5f;
	ImVec4 colBg = *v ? kMint : ImVec4(0.25f,0.28f,0.33f,1.f);
	if (hovered) colBg.w = 1.0f;
	dl->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(colBg), r);
	dl->AddRect(p0, p1, ImGui::ColorConvertFloat4ToU32(kBorder), r, 0, 1.0f);
	float knobX = *v ? (p0.x + size.x - r - 2) : (p0.x + 2);
	dl->AddCircleFilled(ImVec2(knobX + r, p0.y + r), r - 3, ImGui::ColorConvertFloat4ToU32(ImVec4(0.98f,0.98f,0.98f,1.f)), 16);
	if (clicked) 
	return clicked;
}

// 2x2 selectable card for Fill mode (Figma style)
bool FillModeCard(const char* label, bool selected, const ImVec2& sz) {
	ImVec4 bg = selected ? ImVec4(0.18f,0.33f,0.27f,1.f) : kCardBg;
	ImVec4 bd = selected ? kMint : kBorder;
	ImGui::PushStyleColor(ImGuiCol_Button, bg);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(bg.x*1.15f, bg.y*1.15f, bg.z*1.15f, 1.f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, bg);
	ImGui::PushStyleColor(ImGuiCol_Border, bd);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
	bool clicked = ImGui::Button(label, sz);
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(4);
	if (selected) {
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 a = ImGui::GetItemRectMin();
		dl->AddCircleFilled(ImVec2(a.x+14, a.y+ sz.y*0.5f), 6, ImGui::ColorConvertFloat4ToU32(kMint), 12);
		dl->AddCircleFilled(ImVec2(a.x+14, a.y+ sz.y*0.5f), 3, ImGui::ColorConvertFloat4ToU32(ImVec4(0.09f,0.12f,0.16f,1.f)), 8);
	}
	return clicked;
}

bool CatalogHeader(const char* id, const char* title, const char* subtitle, const char* num, bool* toggle = nullptr) {
	ImGui::PushID(id);
	ImVec2 avail = ImGui::GetContentRegionAvail();
	float w = avail.x;
	// card container
	ImGui::PushStyleColor(ImGuiCol_ChildBg, kCardBg);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12,10));
	ImGui::BeginChild(id, ImVec2(w, 0), true, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar);

	bool& open = g_catalog_open[id];
	// header row
	ImGui::TextColored(kMint, "%s", num);
	ImGui::SameLine();
	ImGui::TextColored(kText, "%s", title);
	ImGui::SameLine();
	// number and chevron on right
	float rightX = ImGui::GetWindowWidth() - 60;
	if (toggle) {
		ImGui::SameLine(rightX - 44);
		AlexaToggle((std::string("tog_")+id).c_str(), toggle);
		ImGui::SameLine();
	}
	// expand button
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 32);
	if (ImGui::SmallButton(open ? "v##exp" : ">##exp")) open = !open;

	if (subtitle && subtitle[0]) {
		ImGui::TextDisabled("%s", subtitle);
	}
	ImGui::Separator();
	ImGui::Spacing();
	bool wantContent = open;
	if (!wantContent) {
		ImGui::EndChild();
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(1);
		ImGui::PopID();
		return false;
	}
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(1);
	// content will be drawn inside same child, caller must EndChild
	return true;
}
inline void CatalogFooter() {
	ImGui::Spacing();
	ImGui::EndChild();
	ImGui::PopID();
	ImGui::Spacing();
}

void DrawAlexaHeader() {
	// Top banner: logo placeholder + title + version + expand controls
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f,0.15f,0.19f,1.f));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.f);
	ImGui::BeginChild("alexa_header", ImVec2(ImGui::GetContentRegionAvail().x, 58), true);
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 p0 = ImGui::GetCursorScreenPos();

	// Logo square with gradient (fallback if texture not loaded)
	ImVec2 logoPos = ImVec2(p0.x + 8, p0.y + 6);
	ImVec2 logoSize(42,42);
	dl->AddRectFilled(logoPos, ImVec2(logoPos.x+logoSize.x, logoPos.y+logoSize.y), IM_COL32(0x00,0xD6,0xFF,255), 8.f);
	dl->AddRectFilled(ImVec2(logoPos.x, logoPos.y+20), ImVec2(logoPos.x+logoSize.x, logoPos.y+logoSize.y), IM_COL32(0x00,0x20,0x71,255), 8.f);
	dl->AddText(ImVec2(logoPos.x+10, logoPos.y+11), IM_COL32(255,255,255,255), "AI");
	ImGui::SetCursorPosX(62);
	ImGui::SetCursorPosY(8);
	ImGui::TextColored(ImVec4(0.00f,0.84f,1.00f,1.f), "ALEXAINC");
	ImGui::SameLine();
	ImGui::TextColored(kText, " // CHAMS");
	ImGui::SameLine();
	ImGui::TextDisabled("Graphics Enhancer  v3.2");
	ImGui::SetCursorPosX(62);
	ImGui::TextDisabled("by hazu@alexainc  •  catalog panel  •  INSERT to toggle");
	ImGui::SameLine();
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 140);
	if (ImGui::SmallButton("Expand all")) { for(auto& kv:g_catalog_open) kv.second=true; }
	ImGui::SameLine();
	if (ImGui::SmallButton("Collapse")) { for(auto& kv:g_catalog_open) kv.second=false; }
	ImGui::EndChild();
	ImGui::PopStyleVar(1);
	ImGui::PopStyleColor(1);
}

} // anonymous

void reshade::runtime::draw_gui_chams()
{
	static bool settings_loaded = false;
	if (!settings_loaded) {
		chams::load();
		settings_loaded = true;
	}
	chams::settings &s = chams::g_settings;
	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);

	// Apply dark catalog theme for this page
	ImGui::PushStyleColor(ImGuiCol_WindowBg, kBg);
	ImGui::PushStyleColor(ImGuiCol_Text, kText);
	ImGui::PushStyleColor(ImGuiCol_Border, kBorder);

	DrawAlexaHeader();

	// ---------- Stage 97: Catalog Search & Filter ----------
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10,6));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.14f,0.17f,0.21f,1.f));
		ImGui::PushStyleColor(ImGuiCol_TextDisabled, kTextDim);
		// Use full width
		float w = ImGui::GetContentRegionAvail().x;
		ImGui::SetNextItemWidth(w);
		bool changed = ImGui::InputTextWithHint("##catalog_search", "Search panels, settings... (e.g. glow, fps, walls)", g_search_buf, sizeof(g_search_buf));
		if (changed) {
			// On search change, Expand All behaviour is preserved but we auto-show matches by forcing open in each panel below
		}
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);
		if (g_search_buf[0]!='\0') {
			ImGui::TextDisabled("Showing matches for \"%s\" — collapsed sections auto-expanded", g_search_buf);
		}
		// Stage99: count matches for explicit no-result UX (no config mutation)
		if (g_search_buf[0]!='\0') {
			bool _m_master = SectionMatches("Master switch", "Master switch and runtime capture", "Master switch F8 Runtime capture Master enabled");
			bool _m_render = SectionMatches("Render mode", "Direct / Overlay", "Render mode Direct Overlay See enemies through walls glue through walls renderer");
			bool _m_detect = SectionMatches("Character detection", "How the shader is identified", "Character detection Strict Wide Skinned All hair cloth wide match BackLightDir avatar Skinned bone Wide mixed");
			bool _m_glow   = SectionMatches("Glow & fill", "Control body fill", "Glow fill Player highlight Fill mode Solid colour Blend Real clothes Glow strength Through walls Open part");
			bool _m_stroke = SectionMatches("Stroke (2D outline)", "One constant-width line", "Stroke 2D outline Stroke size open covered Through walls Open part");
			bool _m_wire   = SectionMatches("Wireframe (3D)", "3D wireframe", "Wireframe Wire size Wire strength Wire through walls Wire open");
			bool _m_own    = SectionMatches("Third person", "My own character", "Third person My own character No chams on my own character Safety cap own distance hide_own");
			bool _m_fps    = SectionMatches("FPS Counter", "fixed — always show", "FPS Counter OSD text size OSD text color OSD position Show FPS fps fps_scale show_fps");
			bool _m_logs   = SectionMatches("Diagnostics & Logs", "Diagnostics & Logs", "Diagnostics Logs dump Refresh logs Open log folder chams-dump.txt");
			int _visible = (_m_master?1:0)+(_m_render?1:0)+(_m_detect?1:0)+(_m_glow?1:0)+(_m_stroke?1:0)+(_m_wire?1:0)+(_m_own?1:0)+(_m_fps?1:0)+(_m_logs?1:0);
			if (_visible==0) {
				ImGui::Spacing();
				ImGui::TextColored(kPink, "No matches found for \"%s\" \u2014 try glow, fps, walls, or clear search", g_search_buf);
				ImGui::TextDisabled("Tip: search is live, case-insensitive, matches titles, subtitles and setting names");
			}
		}
	}
	ImGui::Separator();

	// ---------- 01 Master & Capture (fixed: effective expand without mutating g_catalog_open) ----------
	bool &o_master = g_catalog_open["master"];
	bool isSearchMaster = g_search_buf[0]!='\0';
	bool matches_master = SectionMatches("Master switch", "Master switch and runtime capture", "Master switch F8 Runtime capture Master enabled");
	if (!matches_master) { /* filtered */ } else {
	bool effective_master = o_master || (isSearchMaster && matches_master);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, kCardBg);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.f);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12,10));
	ImGui::BeginChild("panel_master", ImVec2(ImGui::GetContentRegionAvail().x,0), true, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::TextColored(kMint, "01"); ImGui::SameLine(); ImGui::Text("Master switch and runtime capture"); ImGui::SameLine(ImGui::GetWindowWidth()-30); if(ImGui::SmallButton(o_master?"v##m":" >##m")) o_master=!o_master;
	if (true) { // toggle on right similar to CatalogHeader
		float rightX = ImGui::GetWindowWidth() - 60;
		// keep same line toggle for Master enabled
		ImGui::SameLine(rightX - 44);
		AlexaToggle("tog_master", &s.enabled);
	}
	ImGui::TextDisabled("F8 toggles in game \u2022 Master enables all chams");
	ImGui::Separator();
	if(effective_master){
		if (!s.enabled) ImGui::TextColored(ImVec4(1.f,0.5f,0.5f,1.f), "Chams are OFF \u2014 no effect in game");
		if (ImGui::TreeNode("Runtime capture (experimental)")) {
			ImGui::TextDisabled("Diagnostic trace \u2014 not required for play");
			ImGui::TreePop();
		}
	}
	ImGui::EndChild(); ImGui::PopStyleVar(3); ImGui::PopStyleColor(1); ImGui::Spacing();
	}

	// Re-do correctly: our helper returns false when closed and already popped, so we need branches
	// To keep logic simple, we use manual check after header
	// Instead we reimplement inline panels below for clarity

	// We already closed 01, continue with explicit panels using same helper pattern but with correct open tracking

	// ---------- 02 Render ----------
	{
	bool &o = g_catalog_open["render"];
	bool isSearch = g_search_buf[0]!='\0';
	bool matches_render = SectionMatches("Render mode", "Direct / Overlay", "Render mode Direct Overlay See enemies through walls glue through walls renderer");
	if (!matches_render) { /* skip panel entirely when filtering */ } else {
	bool effective_render = o || (isSearch && matches_render);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, kCardBg);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.f);
	ImGui::BeginChild("panel_render", ImVec2(ImGui::GetContentRegionAvail().x,0), true, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::TextColored(kMint, "02"); ImGui::SameLine(); ImGui::Text("Render mode"); ImGui::SameLine(ImGui::GetWindowWidth()-30); if(ImGui::SmallButton(o?"v##r":" >##r")) o=!o;
	ImGui::TextDisabled("Direct (recommended) / Overlay (experimental)");
	ImGui::Separator();
	if(effective_render){
		if (ImGui::Combo("##renderer", &s.render_mode, "Overlay - mask framebuffer (glue wall safe)\0Direct - per-draw passes (safe default)\0"))
			chams::save();
		ImGui::TextDisabled("Overlay paints on top of final frame — nothing can hide it.");
		ImGui::TextDisabled("Direct is depth-tested — if screen goes black, switch to Direct.");
		ImGui::Spacing();
		if (ImGui::Checkbox("See enemies through walls and glue / ice wall##through", &s.through_walls))
			chams::save();
		ImGui::TextDisabled("ON = depth pushed forward so wall cannot paint over chams");
	}
	ImGui::EndChild(); ImGui::PopStyleVar(1); ImGui::PopStyleColor(1); ImGui::Spacing();
	}
	}

	// ---------- 03 Detection ----------
	{
	bool &o = g_catalog_open["detect"];
	bool isSearch = g_search_buf[0]!='\0';
	bool matches_detect = SectionMatches("Character detection", "How the shader is identified", "Character detection Strict Wide Skinned All hair cloth wide match BackLightDir avatar Skinned bone Wide mixed");
	if (!matches_detect) {} else {
	bool effective_detect = o || (isSearch && matches_detect);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, kCardBg);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.f);
	ImGui::BeginChild("panel_detect", ImVec2(ImGui::GetContentRegionAvail().x,0), true, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::TextColored(kMint, "03"); ImGui::SameLine(); ImGui::Text("Character detection"); ImGui::SameLine(ImGui::GetWindowWidth()-30); if(ImGui::SmallButton(o?"v##d":" >##d")) o=!o;
	ImGui::TextDisabled("How the shader is identified — locked discriminator");
	ImGui::Separator();
	if(effective_detect){
		if (ImGui::Combo("##detect", &s.detect_mode, "Strict - _CharaLightIntensity\0Wide - any avatar-only uniform [default]\0Skinned - bone matrix uniform\0All - union of three\0"))
			chams::save();
		ImGui::BulletText("Strict: original DLL rule — _CharaLightIntensity");
		ImGui::BulletText("Wide: any uniforms will detect and ff coins and some special car skins will detect");
		ImGui::BulletText("Skinned: detect by bone — some 2D shader kits may not detect");
		ImGui::BulletText("Mixed (All): all mixed — union for maximum coverage");
		if (ImGui::Checkbox("Also paint hair / cloth shaders (wide match)##wide", &s.wide_match))
			chams::save();
		ImGui::TextDisabled("Matched by _BackLightDir — shaders without _CharaLightIntensity");
	}
	ImGui::EndChild(); ImGui::PopStyleVar(1); ImGui::PopStyleColor(1); ImGui::Spacing();
	}

	// ---------- 04 Glow & Fill (Figma exact) ----------
	{
	bool &o = g_catalog_open["glow"];
	bool isSearch = g_search_buf[0]!='\0';
	bool matches_glow = SectionMatches("Glow & fill", "Control body fill", "Glow fill Player highlight Fill mode Solid colour Blend Real clothes Glow strength Through walls Open part");
	if (!matches_glow) {} else {
	bool effective_glow = o || (isSearch && matches_glow);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, kCardBg);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.f);
	ImGui::BeginChild("panel_glow", ImVec2(ImGui::GetContentRegionAvail().x,0), true, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::TextColored(kTextDim, "04"); ImGui::SameLine(); ImGui::Text("Glow & fill"); ImGui::SameLine();
	ImGui::TextDisabled("Control body fill, clothing detail and visible / hidden colours.");
	ImGui::SameLine(ImGui::GetWindowWidth()-70); AlexaToggle("glow_tog", &s.glow);
	ImGui::Separator();
	if(effective_glow){
		// Fill mode 2x2 grid
		ImGui::TextDisabled("Fill mode");
		ImVec2 avail = ImGui::GetContentRegionAvail();
		float bw = (avail.x - 8) * 0.5f;
		ImVec2 bsz(bw, 30);
		bool c0 = s.glow_mode==0, c1=s.glow_mode==1, c2=s.glow_mode==2, c3=s.glow_mode==3;
		if (FillModeCard("  Solid colour", c0, bsz) && !c0) { s.glow_mode=0; chams::save(); }
		ImGui::SameLine();
		if (FillModeCard("  Blend", c1, bsz) && !c1) { s.glow_mode=1; chams::save(); }
		if (FillModeCard("  Real clothes + light colour", c2, bsz) && !c2) { s.glow_mode=2; chams::save(); }
		ImGui::SameLine();
		if (FillModeCard("  Real clothes only", c3, bsz) && !c3) { s.glow_mode=3; chams::save(); }
		if (s.glow_mode==1) ImGui::TextDisabled("Blend preserves real clothes and layers colour over them.");
		else if (s.glow_mode==0) ImGui::TextDisabled("Whole body filled with solid colour — classic chams.");
		else ImGui::TextDisabled("Real clothes drawn through walls — best for outfit visibility.");

		ImGui::Spacing();
		ImGui::Text("Glow strength"); ImGui::SameLine(ImGui::GetWindowWidth()-60); ImGui::TextDisabled("%.2f", s.glow_alpha);
		if (ImGui::SliderFloat("##glowalpha", &s.glow_alpha, 0.05f, 1.0f, "")) chams::save();
		ImGui::TextDisabled("0.05                                    1.00");
		ImGui::Spacing();
		ImGui::Text("Through walls - colour"); ImGui::SameLine(ImGui::GetWindowWidth()-150);
		if (ImGui::ColorEdit3("##glowthrough", s.glow_through_color, ImGuiColorEditFlags_NoInputs)) chams::save();
		ImGui::SameLine(); ImGui::TextDisabled("#%02X%02X%02X", (int)(s.glow_through_color[0]*255), (int)(s.glow_through_color[1]*255), (int)(s.glow_through_color[2]*255));
		ImGui::Text("Open part - colour"); ImGui::SameLine(ImGui::GetWindowWidth()-150);
		AlexaToggle("glow_open_tog", &s.glow_open); ImGui::SameLine();
		if (s.glow_open) { if (ImGui::ColorEdit3("##glowopen", s.glow_open_color, ImGuiColorEditFlags_NoInputs)) chams::save(); }
		else ImGui::TextDisabled("off");
	}
	ImGui::EndChild(); ImGui::PopStyleVar(1); ImGui::PopStyleColor(1); ImGui::Spacing();
	}
	}

	// ---------- 05 Stroke ----------
	{
	bool &o = g_catalog_open["stroke"];
	bool isSearch = g_search_buf[0]!='\0';
	bool matches_stroke = SectionMatches("Stroke (2D outline)", "One constant-width line", "Stroke 2D outline Stroke size open covered Through walls Open part");
	if (!matches_stroke) {} else {
	bool effective_stroke = o || (isSearch && matches_stroke);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, kCardBg);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.f);
	ImGui::BeginChild("panel_stroke", ImVec2(ImGui::GetContentRegionAvail().x,0), true, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::TextColored(kMint, "05"); ImGui::SameLine(); ImGui::Text("Stroke (2D outline)"); ImGui::SameLine(ImGui::GetWindowWidth()-70); AlexaToggle("stroke_tog", &s.stroke);
	ImGui::TextDisabled("One constant-width line on outer side — never covers body");
	ImGui::Separator();
	if(effective_stroke){
		if (ImGui::SliderInt("Stroke size - open (px)", &s.stroke_open_size, 1, 16)) chams::save();
		if (ImGui::SliderInt("Stroke size - covered (px)", &s.stroke_through_size, 1, 16)) chams::save();
		ImGui::TextDisabled("open = plain sight | covered = behind wall/glue/car");
		if (ImGui::SliderFloat("Stroke strength##salpha", &s.stroke_alpha, 0.05f, 1.0f, "%.2f")) chams::save();
		ImGui::Text("Through walls - colour"); ImGui::SameLine(ImGui::GetWindowWidth()-150);
		if (ImGui::ColorEdit3("##stthrough", s.stroke_through_color, ImGuiColorEditFlags_NoInputs)) chams::save();
		ImGui::Text("Open part - colour"); ImGui::SameLine(ImGui::GetWindowWidth()-150);
		AlexaToggle("stopen_tog", &s.stroke_open); ImGui::SameLine();
		if (s.stroke_open) if (ImGui::ColorEdit3("##stopen", s.stroke_open_color, ImGuiColorEditFlags_NoInputs)) chams::save();
	}
	ImGui::EndChild(); ImGui::PopStyleVar(1); ImGui::PopStyleColor(1); ImGui::Spacing();
	}
	}
	// wire as nested inside stroke catalog for compactness but also separate toggle
	bool &ow = g_catalog_open["wire"];
	bool isSearchWire = g_search_buf[0]!='\0';
	bool matches_wire = SectionMatches("Wireframe (3D)", "3D wireframe", "Wireframe Wire size Wire strength Wire through walls Wire open");
	bool show_wire_panel = !isSearchWire || matches_wire; // decoupled from stroke (Stage99 fix)
	if (!show_wire_panel) {} else {
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f,0.15f,0.18f,1.f));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.f);
	ImGui::BeginChild("panel_wire", ImVec2(ImGui::GetContentRegionAvail().x,0), true, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::TextColored(kTextDim, "06"); ImGui::SameLine(); ImGui::Text("Wireframe (3D)"); ImGui::SameLine(ImGui::GetWindowWidth()-70); AlexaToggle("wire_tog", &s.wire);
	ImGui::Separator();
	if(ow || s.wire){
		if (s.wire) {
			if (ImGui::SliderInt("Wire size (px)", &s.wire_size, 1, 8)) chams::save();
			if (ImGui::SliderFloat("Wire strength##walpha", &s.wire_alpha, 0.05f, 1.0f, "%.2f")) chams::save();
			if (ImGui::ColorEdit3("Wire through walls", s.wire_through_color, ImGuiColorEditFlags_NoInputs)) chams::save();
			if (ImGui::Checkbox("Wire open part##wireopen", &s.wire_open)) chams::save();
			if (s.wire_open) if (ImGui::ColorEdit3("Wire open##wireopen2", s.wire_open_color, ImGuiColorEditFlags_NoInputs)) chams::save();
		}
	}
	ImGui::EndChild(); ImGui::PopStyleVar(1); ImGui::PopStyleColor(1); ImGui::Spacing();
	}
	}

	// ---------- 07 Third person / My own ----------
	{
	bool &o = g_catalog_open["own"];
	bool isSearch = g_search_buf[0]!='\0';
	bool matches_own = SectionMatches("Third person", "My own character", "Third person My own character No chams on my own character Safety cap own distance hide_own");
	if (!matches_own) {} else {
	bool effective_own = o || (isSearch && matches_own);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, kCardBg);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.f);
	ImGui::BeginChild("panel_own", ImVec2(ImGui::GetContentRegionAvail().x,0), true, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::TextColored(kMint, "07"); ImGui::SameLine(); ImGui::Text("Third person — My own character"); ImGui::SameLine(ImGui::GetWindowWidth()-30); if(ImGui::SmallButton(o?"v##o":" >##o")) o=!o;
	ImGui::TextDisabled("Prevent self-chams in third person view");
	ImGui::Separator();
	if(effective_own){
		if (ImGui::Checkbox("No chams on my own character (auto)##hideown", &s.hide_own)) chams::save();
		if (s.hide_own) {
			ImGui::TextDisabled("Closest draw with stable 2.4-2.6 m distance is considered self");
			if (ImGui::SliderFloat("Safety cap: nearer than##owncap", &s.own_distance, 0.5f, 15.0f, "%.1f m")) chams::save();
			ImGui::TextDisabled("Further than this is never treated as mine");
		}
	}
	ImGui::EndChild(); ImGui::PopStyleVar(1); ImGui::PopStyleColor(1); ImGui::Spacing();
	}
	}

	// ---------- 08 FPS (fix) ----------
	{
	bool &o = g_catalog_open["fps"];
	bool isSearch = g_search_buf[0]!='\0';
	bool matches_fps = SectionMatches("FPS Counter", "fixed — always show", "FPS Counter OSD text size OSD text color OSD position Show FPS fps fps_scale show_fps");
	if (!matches_fps) {} else {
	bool effective_fps = o || (isSearch && matches_fps);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, kCardBg);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.f);
	ImGui::BeginChild("panel_fps", ImVec2(ImGui::GetContentRegionAvail().x,0), true, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::TextColored(kMint, "08"); ImGui::SameLine(); ImGui::Text("FPS Counter"); ImGui::SameLine(); ImGui::TextDisabled("(fixed)");
	// Bridge to runtime FPS (this->_show_fps) - independent from chams settings
	bool fpsOn = (_show_fps == 1);
	ImGui::SameLine(ImGui::GetWindowWidth()-70); if (AlexaToggle("fps_tog", &fpsOn)) {
		_show_fps = fpsOn ? 1 : 0;
		// persist to ReShade.ini OVERLAY/ShowFPS
		save_config();
	}
	ImGui::TextDisabled("Turned on but not showing? Now defaults to Always (1) not Overlay-only (2)");
	ImGui::Separator();
	if(effective_fps){
		ImGui::Text("OSD text size"); ImGui::SameLine(ImGui::GetWindowWidth()-60); ImGui::TextDisabled("%.1f", _fps_scale);
		if (ImGui::SliderFloat("##fpsscale", &_fps_scale, 0.2f, 2.5f, "")) save_config();
		if (ImGui::ColorEdit4("OSD text color##fpscol", _fps_col, ImGuiColorEditFlags_AlphaBar)) save_config();
		const char* posItems[] = {"Top left","Top right","Bottom left","Bottom right"};
		int pos = (int)_fps_pos;
		if (ImGui::Combo("OSD position##fpspos", &pos, posItems, 4)) { _fps_pos = pos; save_config(); }
		ImGui::TextDisabled("Press assigned FPS key to toggle • also in Settings > Overlay");
	}
	ImGui::EndChild(); ImGui::PopStyleVar(1); ImGui::PopStyleColor(1); ImGui::Spacing();
	}
	}

	// ---------- 09 Logs ----------
	{
	bool &o = g_catalog_open["logs"];
	bool isSearch = g_search_buf[0]!='\0';
	bool matches_logs = SectionMatches("Diagnostics & Logs", "Diagnostics & Logs", "Diagnostics Logs dump Refresh logs Open log folder chams-dump.txt");
	if (!matches_logs) {} else {
	bool effective_logs = o || (isSearch && matches_logs);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, kCardBg);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.f);
	ImGui::BeginChild("panel_logs", ImVec2(ImGui::GetContentRegionAvail().x,0), true, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::TextColored(kTextDim, "09"); ImGui::SameLine(); ImGui::Text("Diagnostics & Logs"); ImGui::SameLine(ImGui::GetWindowWidth()-30); if(ImGui::SmallButton(o?"v##l":" >##l")) o=!o;
	ImGui::Separator();
	if(effective_logs){
		static std::vector<std::string> s_log_lines; static bool loaded=false;
		if(!loaded){ std::ifstream f(chams::dump_path()); if(f.is_open()){ std::string l; while(std::getline(f,l)) s_log_lines.push_back(l);} loaded=true; }
		if (ImGui::Button("Refresh logs")) { s_log_lines.clear(); loaded=false; }
		ImGui::SameLine(); if (ImGui::Button("Open log folder")) { char p[MAX_PATH*4]={}; GetTempPathA(sizeof(p),p); std::string c="explorer \""+std::string(p)+"\""; system(c.c_str()); }
		ImGui::BeginChild("chamslogscroll", ImVec2(0,120), true);
		int start = (int)s_log_lines.size()>20 ? (int)s_log_lines.size()-20 : 0;
		for(int i=start;i<(int)s_log_lines.size();++i) ImGui::TextColored(ImVec4(0.8f,0.8f,0.8f,1.f), "%s", s_log_lines[i].c_str());
		ImGui::EndChild();
	}
	ImGui::EndChild(); ImGui::PopStyleVar(1); ImGui::PopStyleColor(1); ImGui::Spacing();
	}
	}

	if (ImGui::Button("Reset to defaults")) { s = chams::settings(); _show_fps=1; chams::save(); save_config(); }
	ImGui::SameLine(); ImGui::TextDisabled("created by hazu@alexainc  •  Graphics Enhancer");
	ImGui::SameLine(); ImGui::TextDisabled(" | %s", "catalog v1.0 (Figma)");

	ImGui::PopStyleColor(2);
	ImGui::PopItemWidth();
}

#endif
