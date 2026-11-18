/*
 * chams_menu.cpp -- Beautiful Collapsible Catalog (Figma GpQvYBzm8d1wet9mdajwvY)
 * Redesign 2026-11-18: true collapsible headers, Figma palette #0B0E11/#11171D/#192128/#28323B/#94E8C1, mint toggle 32x18
 * Mirrors AeonSR panel_widgets section()/band logic for cohesive AlexaInc branding.
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
#include <algorithm>

namespace {

// ---------- Figma palette (exact) ----------
// Page #0B0E11, Header #11171D, Card #192128, Border #28323B, Text #EBF0F3, Dim #97A6B2, Mid #687985, Mint #94E8C1, Pink #F09CAD
static const ImVec4 kPage       = ImVec4(11.f/255.f, 14.f/255.f, 17.f/255.f, 1.f);
static const ImVec4 kHeaderBg   = ImVec4(0x11/255.f, 0x17/255.f, 0x1D/255.f, 1.f);
static const ImVec4 kCardBg      = ImVec4(0x19/255.f, 0x21/255.f, 0x28/255.f, 1.f);
static const ImVec4 kBorder      = ImVec4(0x28/255.f, 0x32/255.f, 0x3B/255.f, 1.f);
static const ImVec4 kText        = ImVec4(0xEB/255.f, 0xF0/255.f, 0xF3/255.f, 1.f);
static const ImVec4 kTextDim     = ImVec4(0x97/255.f, 0xA6/255.f, 0xB2/255.f, 1.f);
static const ImVec4 kTextMid     = ImVec4(0x68/255.f, 0x79/255.f, 0x85/255.f, 1.f);
static const ImVec4 kMint        = ImVec4(0x94/255.f, 0xE8/255.f, 0xC1/255.f, 1.f);
static const ImVec4 kMintDim     = ImVec4(0x20/255.f, 0x3D/255.f, 0x34/255.f, 1.f);
static const ImVec4 kPink        = ImVec4(0xF0/255.f, 0x9C/255.f, 0xAD/255.f, 1.f);
static const ImVec4 kCardActive  = ImVec4(0x1C/255.f, 0x2A/255.f, 0x31/255.f, 1.f);

static std::unordered_map<std::string, bool> g_catalog_open = {
	{"master", true}, {"render", true}, {"detect", false}, {"glow", true},
	{"stroke", true}, {"wire", false}, {"own", false}, {"fps", true}, {"logs", false}
};
static char g_search_buf[128] = "";

static std::string ToLowerStr(const std::string &s) {
	std::string r; r.reserve(s.size());
	for (unsigned char c : s) r.push_back((char)std::tolower(c));
	return r;
}
static bool ContainsI(const std::string &hay, const std::string &needle) {
	if (needle.empty()) return true;
	std::string hl = ToLowerStr(hay), nl = ToLowerStr(needle);
	return hl.find(nl) != std::string::npos;
}
static bool SectionMatches(const std::string &title, const std::string &subtitle, const std::string &keywords) {
	if (g_search_buf[0]=='\0') return true;
	std::string needle(g_search_buf);
	return ContainsI(title, needle) || ContainsI(subtitle, needle) || ContainsI(keywords, needle);
}

static float Unit() { return ImGui::GetFontSize() / 13.f; }
static ImU32 Col(const ImVec4 &c) { return ImGui::GetColorU32(c); }
static float Hairline() { return std::max(1.f, std::floor(Unit())); }
static ImVec4 Mix(const ImVec4 &a, const ImVec4 &b, float t) { return ImVec4(a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t, a.w+(b.w-a.w)*t); }
static float CardRounding() { return 8.f * Unit(); }

// ------- Mint toggle 32x18 Figma exact -------
bool AlexaToggle(const char* id, bool* v) {
	ImVec2 sz(32, 18);
	ImGui::InvisibleButton(id, sz);
	bool hovered = ImGui::IsItemHovered();
	bool clicked = ImGui::IsItemClicked();
	if (clicked) *v = !*v;
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 p0 = ImGui::GetItemRectMin();
	ImVec2 p1 = ImVec2(p0.x + sz.x, p0.y + sz.y);
	float r = sz.y * 0.5f;
	ImVec4 bg = *v ? kMint : ImVec4(0.10f,0.13f,0.16f,1.f);
	if (hovered) bg = Mix(bg, kText, 0.06f);
	dl->AddRectFilled(p0, p1, Col(bg), r);
	if (!*v) dl->AddRect(p0, p1, Col(kBorder), r, 0, Hairline());
	float knobR = 6.f;
	float knobX = *v ? (p1.x - knobR - 2.f) : (p0.x + knobR + 2.f);
	float knobY = p0.y + sz.y * 0.5f;
	dl->AddCircleFilled(ImVec2(knobX, knobY), knobR, Col(ImVec4(0.08f,0.10f,0.12f,1.f)), 16);
	if (!*v) dl->AddCircleFilled(ImVec2(knobX, knobY), knobR-0.5f, Col(kTextMid), 16);
	return clicked;
}

void Chevron(ImDrawList* dl, ImVec2 c, bool open, ImVec4 col4) {
	float k = Unit();
	float s = 5.f * k;
	ImU32 col = Col(col4);
	// chevron > rotates 90deg when open
	if (open) {
		// down arrow: 2 lines
		dl->AddLine(ImVec2(c.x - s*0.45f, c.y - s*0.2f), ImVec2(c.x, c.y + s*0.3f), col, 1.6f*k);
		dl->AddLine(ImVec2(c.x + s*0.45f, c.y - s*0.2f), ImVec2(c.x, c.y + s*0.3f), col, 1.6f*k);
	} else {
		dl->AddLine(ImVec2(c.x - s*0.2f, c.y - s*0.45f), ImVec2(c.x + s*0.3f, c.y), col, 1.6f*k);
		dl->AddLine(ImVec2(c.x - s*0.2f, c.y + s*0.45f), ImVec2(c.x + s*0.3f, c.y), col, 1.6f*k);
	}
}

// 2x2 selectable card for Fill mode
bool FillModeCard(const char* label, bool selected, const ImVec2& sz) {
	ImVec4 bg = selected ? ImVec4(0x20/255.f,0x3D/255.f,0x34/255.f,1.f) : kCardBg;
	ImVec4 bd = selected ? kMint : kBorder;
	ImGui::PushStyleColor(ImGuiCol_Button, bg);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Mix(bg, kText, 0.06f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, Mix(bg, kMint, 0.12f));
	ImGui::PushStyleColor(ImGuiCol_Border, bd);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, Hairline());
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f*Unit());
	bool clicked = ImGui::Button(label, sz);
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(4);
	if (selected) {
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 a = ImGui::GetItemRectMin();
		dl->AddCircleFilled(ImVec2(a.x+13, a.y+ sz.y*0.5f), 5*Unit(), Col(kMint), 12);
		dl->AddCircleFilled(ImVec2(a.x+13, a.y+ sz.y*0.5f), 2.2f*Unit(), Col(kHeaderBg), 8);
	}
	return clicked;
}



bool BeginSectionV2(const char* id, const char* num, const char* title, const char* subtitle, bool* toggle, bool forcedOpen)
{
	float k = Unit();
	bool &open = g_catalog_open[id];
	bool eff = forcedOpen ? true : open;
	ImVec2 cur = ImGui::GetCursorScreenPos();
	float w = ImGui::GetContentRegionAvail().x;
	float headerH = ImGui::GetFrameHeight() + 10.f*k;
	if (subtitle && subtitle[0]) headerH += 14.f*k;
	float rounding = CardRounding();

	ImGui::PushID(id);
	bool headerPressed = ImGui::InvisibleButton("##hdr", ImVec2(w, headerH));
	bool hovered = ImGui::IsItemHovered();
	bool held = ImGui::IsItemActive();
	if (headerPressed) {
		// check toggle hit
		ImVec2 mp = ImGui::GetIO().MousePos;
		bool overToggle = false;
		if (toggle) {
			float tl = cur.x + w - 60*k;
			if (mp.x >= tl && mp.x <= tl+32 && mp.y >= cur.y && mp.y <= cur.y+headerH) overToggle = true;
		}
		if (!overToggle) open = !open;
	}

	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec4 hdrCol = held ? Mix(kHeaderBg, kText, 0.04f) : hovered ? Mix(kHeaderBg, kText, 0.03f) : kHeaderBg;
	ImDrawFlags hc = eff ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll;
	dl->AddRectFilled(cur, ImVec2(cur.x+w, cur.y+headerH), Col(hdrCol), rounding, hc);
	dl->AddRect(cur, ImVec2(cur.x+w, cur.y+headerH), Col(kBorder), rounding, hc, Hairline());
	dl->AddRectFilled(ImVec2(cur.x+rounding, cur.y+Hairline()), ImVec2(cur.x+w-rounding, cur.y+Hairline()*2), Col(ImVec4(1,1,1,0.05f)));

	float pad = 12.f*k;
	float numX = cur.x + pad;
	float ty = cur.y + 9.f*k;
	// num in mint
	ImVec2 numSz = ImGui::CalcTextSize(num);
	dl->AddText(ImVec2(numX, ty), Col(kMint), num);
	float tx = numX + numSz.x + 8.f*k;
	dl->AddText(ImVec2(tx, ty), Col(kText), title);
	float chevX = cur.x + w - 14.f*k;
	float chevY = cur.y + headerH*0.38f;
	Chevron(dl, ImVec2(chevX, chevY), eff, hovered?kText:kTextDim);
	if (toggle) {
		ImVec2 tp = ImVec2(cur.x + w - 58.f*k, cur.y + (headerH - 18)/2.f - (subtitle?4*k:0));
		ImGui::SetCursorScreenPos(tp);
		ImGui::PushID("tog");
		AlexaToggle("##t", toggle);
		ImGui::PopID();
	}
	if (subtitle && subtitle[0]) {
		float sy = ty + ImGui::GetFontSize() + 3*k;
		dl->AddText(ImVec2(tx, sy), Col(kTextDim), subtitle);
	}
	ImGui::PopID();
	ImGui::SetCursorScreenPos(ImVec2(cur.x, cur.y+headerH));
	s_headerBottomMap[id] = cur.y + headerH;

	if (!eff) {
		ImGui::Dummy(ImVec2(0,7*k));
		return false;
	}
	// Content area BeginChild with proper bg
	ImGui::PushStyleColor(ImGuiCol_ChildBg, kCardBg);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10*k, 9*k));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, Hairline());
	// We draw content as child; height auto
	bool began = ImGui::BeginChild((std::string(id)+"_body").c_str(), ImVec2(w,0), true, ImGuiWindowFlags_AlwaysAutoResize);
	(void)began;
	return true;
}
void EndSectionV2() {
	float k = Unit();
	ImGui::EndChild();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(1);
	ImGui::Dummy(ImVec2(0,7*k));
}

void DrawAlexaHeader() {
	float k = Unit();
	float w = ImGui::GetContentRegionAvail().x;
	float h = 58;
	ImVec2 cur = ImGui::GetCursorScreenPos();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	float r = 12.f*k;
	// Bg #11171D rounded
	dl->AddRectFilled(cur, ImVec2(cur.x+w, cur.y+h), Col(kHeaderBg), r);
	dl->AddRect(cur, ImVec2(cur.x+w, cur.y+h), Col(kBorder), r, 0, Hairline());
	// Logo 42x42 with gradient fallback: try to show Alexa logo via rect + AI text (texture load would be ideal)
	ImVec2 logoPos = ImVec2(cur.x + 8, cur.y + 8);
	ImVec2 logoSz(42,42);
	dl->AddRectFilled(logoPos, ImVec2(logoPos.x+logoSz.x, logoPos.y+logoSz.y), IM_COL32(0x00,0xD6,0xFF,255), 8.f);
	dl->AddRectFilled(ImVec2(logoPos.x, logoPos.y+20), ImVec2(logoPos.x+logoSz.x, logoPos.y+logoSz.y), IM_COL32(0x00,0x20,0x71,255), 8.f);
	dl->AddText(ImVec2(logoPos.x+10, logoPos.y+11), IM_COL32(255,255,255,255), "AI");

	ImGui::SetCursorScreenPos(ImVec2(cur.x + 62, cur.y + 8));
	ImGui::TextColored(ImVec4(0.00f,0.84f,1.00f,1.f), "ALEXAINC");
	ImGui::SameLine(0, 6.f*k); ImGui::TextColored(kText, "// CHAMS");
	ImGui::SameLine(0, 8.f*k); ImGui::TextDisabled("Graphics Enhancer v3.2");
	// second line
	ImGui::SetCursorScreenPos(ImVec2(cur.x + 62, cur.y + 26));
	ImGui::TextDisabled("by hazu@alexainc  •  catalog panel  •  INSERT to toggle");
	// Expand/Collapse on right
	ImGui::SetCursorScreenPos(ImVec2(cur.x + w - 150, cur.y + 14));
	if (ImGui::SmallButton("Expand all")) for(auto &kv: g_catalog_open) kv.second=true;
	ImGui::SameLine();
	if (ImGui::SmallButton("Collapse")) for(auto &kv: g_catalog_open) kv.second=false;

	ImGui::SetCursorScreenPos(ImVec2(cur.x, cur.y+h+8));
}

} // anon

void reshade::runtime::draw_gui_chams()
{
	static bool settings_loaded = false;
	if (!settings_loaded) { chams::load(); settings_loaded = true; }
	chams::settings &s = chams::g_settings;
	// Page bg
	ImGui::PushStyleColor(ImGuiCol_WindowBg, kPage);
	ImGui::PushStyleColor(ImGuiCol_Text, kText);
	ImGui::PushStyleColor(ImGuiCol_Border, kBorder);
	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.60f);

	DrawAlexaHeader();

	// Search
	{
		float k = Unit();
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f * k);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10,6));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0x19/255.f,0x21/255.f,0x28/255.f,1.f));
		float w = ImGui::GetContentRegionAvail().x;
		ImGui::SetNextItemWidth(w);
		ImGui::InputTextWithHint("##catalog_search", "Search panels, settings... (e.g. glow, fps, walls)", g_search_buf, sizeof(g_search_buf));
		ImGui::PopStyleColor(1);
		ImGui::PopStyleVar(2);
		if (g_search_buf[0]!='\0') ImGui::TextDisabled("Showing matches for \"%s\" — collapsed sections auto-expanded", g_search_buf);
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
			int _vis = (_m_master?1:0)+(_m_render?1:0)+(_m_detect?1:0)+(_m_glow?1:0)+(_m_stroke?1:0)+(_m_wire?1:0)+(_m_own?1:0)+(_m_fps?1:0)+(_m_logs?1:0);
			if (_vis==0) {
				ImGui::Spacing();
				ImGui::TextColored(kPink, "No matches found for \"%s\" — try glow, fps, walls, or clear search", g_search_buf);
				ImGui::TextDisabled("Tip: search is live, case-insensitive");
			}
		}
	}
	ImGui::Separator();
	ImGui::Spacing();

	// ---------- 01 Master ----------
	{
		bool isSearchMaster = g_search_buf[0]!='\0';
		bool matches_master = SectionMatches("Master switch", "Master switch and runtime capture", "Master switch F8 Runtime capture Master enabled");
		if (matches_master) {
			bool forced = isSearchMaster && matches_master;
			bool has = true;
			if (BeginSectionV2("master", "01", "Master switch and runtime capture", "F8 toggles in game • Master enables all chams", &s.enabled, forced)) {
				if (!s.enabled) ImGui::TextColored(ImVec4(1,0.5f,0.5f,1), "Chams are OFF — no effect in game");
				if (ImGui::TreeNode("Runtime capture (experimental)")) {
					ImGui::TextDisabled("Diagnostic trace — not required for play");
					ImGui::TreePop();
				}
				EndSectionV2();
			}
		}
	}
	// ---------- 02 Render ----------
	{
		bool isSearch = g_search_buf[0]!='\0';
		bool matches = SectionMatches("Render mode", "Direct / Overlay", "Render mode Direct Overlay See enemies through walls glue through walls renderer");
		if (matches) {
			bool forced = isSearch && matches;
			if (BeginSectionV2("render", "02", "Render mode", "Direct (recommended) / Overlay (experimental)", nullptr, forced)) {
				if (ImGui::Combo("##renderer", &s.render_mode, "Overlay - mask framebuffer (glue wall safe)\0Direct - per-draw passes (safe default)\0")) chams::save();
				ImGui::TextDisabled("Overlay paints on top of final frame — nothing can hide it.");
				ImGui::TextDisabled("Direct is depth-tested — if screen goes black, switch to Direct.");
				ImGui::Spacing();
				if (ImGui::Checkbox("See enemies through walls and glue / ice wall##through", &s.through_walls)) chams::save();
				ImGui::TextDisabled("ON = depth pushed forward so wall cannot paint over chams");
				EndSectionV2();
			}
		}
	}
	// ---------- 03 Detection ----------
	{
		bool isSearch = g_search_buf[0]!='\0';
		bool matches = SectionMatches("Character detection", "How the shader is identified", "Character detection Strict Wide Skinned All hair cloth wide match BackLightDir avatar Skinned bone Wide mixed");
		if (matches) {
			bool forced = isSearch && matches;
			if (BeginSectionV2("detect", "03", "Character detection", "How the shader is identified — locked discriminator", nullptr, forced)) {
				if (ImGui::Combo("##detect", &s.detect_mode, "Strict - _CharaLightIntensity\0Wide - any avatar-only uniform [default]\0Skinned - bone matrix uniform\0All - union of three\0")) chams::save();
				ImGui::BulletText("Strict: original DLL rule — _CharaLightIntensity");
				ImGui::BulletText("Wide: any uniforms will detect and ff coins and some special car skins will detect");
				ImGui::BulletText("Skinned: detect by bone — some 2D shader kits may not detect");
				ImGui::BulletText("Mixed (All): all mixed — union for maximum coverage");
				if (ImGui::Checkbox("Also paint hair / cloth shaders (wide match)##wide", &s.wide_match)) chams::save();
				ImGui::TextDisabled("Matched by _BackLightDir — shaders without _CharaLightIntensity");
				EndSectionV2();
			}
		}
	}
	// ---------- 04 Glow & Fill ----------
	{
		bool isSearch = g_search_buf[0]!='\0';
		bool matches = SectionMatches("Glow & fill", "Control body fill", "Glow fill Player highlight Fill mode Solid colour Blend Real clothes Glow strength Through walls Open part");
		if (matches) {
			bool forced = isSearch && matches;
			if (BeginSectionV2("glow", "04", "Glow & fill", "Control body fill, clothing detail and visible / hidden colours.", &s.glow, forced)) {
				ImGui::TextDisabled("Fill mode");
				ImVec2 avail = ImGui::GetContentRegionAvail();
				float bw = (avail.x - 8) * 0.5f;
				ImVec2 bsz(bw, 30);
				bool c0 = s.glow_mode==0, c1=s.glow_mode==1, c2=s.glow_mode==2, c3=s.glow_mode==3;
				if (FillModeCard("  Solid colour", c0, bsz) && !c0) { s.glow_mode=0; chams::save(); } ImGui::SameLine();
				if (FillModeCard("  Blend", c1, bsz) && !c1) { s.glow_mode=1; chams::save(); }
				if (FillModeCard("  Real clothes + light colour", c2, bsz) && !c2) { s.glow_mode=2; chams::save(); } ImGui::SameLine();
				if (FillModeCard("  Real clothes only", c3, bsz) && !c3) { s.glow_mode=3; chams::save(); }
				if (s.glow_mode==1) ImGui::TextDisabled("Blend preserves real clothes and layers colour over them.");
				else if (s.glow_mode==0) ImGui::TextDisabled("Whole body filled with solid colour — classic chams.");
				else ImGui::TextDisabled("Real clothes drawn through walls — best for outfit visibility.");
				ImGui::Spacing();
				ImGui::Text("Glow strength"); ImGui::SameLine(ImGui::GetWindowWidth()-60); ImGui::TextDisabled("%.2f", s.glow_alpha);
				if (ImGui::SliderFloat("##glowalpha", &s.glow_alpha, 0.05f, 1.f, "")) chams::save();
				ImGui::TextDisabled("0.05                                    1.00");
				ImGui::Spacing();
				ImGui::Text("Through walls - colour"); ImGui::SameLine(ImGui::GetWindowWidth()-150);
				if (ImGui::ColorEdit3("##glowthrough", s.glow_through_color, ImGuiColorEditFlags_NoInputs)) chams::save();
				ImGui::SameLine(); ImGui::TextDisabled("#%02X%02X%02X", (int)(s.glow_through_color[0]*255), (int)(s.glow_through_color[1]*255), (int)(s.glow_through_color[2]*255));
				ImGui::Text("Open part - colour"); ImGui::SameLine(ImGui::GetWindowWidth()-150);
				AlexaToggle("glow_open_tog", &s.glow_open); ImGui::SameLine();
				if (s.glow_open) { if (ImGui::ColorEdit3("##glowopen", s.glow_open_color, ImGuiColorEditFlags_NoInputs)) chams::save(); } else ImGui::TextDisabled("off");
				EndSectionV2();
			}
		}
	}
	// ---------- 05 Stroke ----------
	{
		bool isSearch = g_search_buf[0]!='\0';
		bool matches = SectionMatches("Stroke (2D outline)", "One constant-width line", "Stroke 2D outline Stroke size open covered Through walls Open part");
		if (matches) {
			bool forced = isSearch && matches;
			if (BeginSectionV2("stroke", "05", "Stroke (2D outline)", "One constant-width line on outer side — never covers body", &s.stroke, forced)) {
				if (ImGui::SliderInt("Stroke size - open (px)", &s.stroke_open_size, 1, 16)) chams::save();
				if (ImGui::SliderInt("Stroke size - covered (px)", &s.stroke_through_size, 1, 16)) chams::save();
				ImGui::TextDisabled("open = plain sight | covered = behind wall/glue/car");
				if (ImGui::SliderFloat("Stroke strength##salpha", &s.stroke_alpha, 0.05f, 1.f, "%.2f")) chams::save();
				ImGui::Text("Through walls - colour"); ImGui::SameLine(ImGui::GetWindowWidth()-150);
				if (ImGui::ColorEdit3("##stthrough", s.stroke_through_color, ImGuiColorEditFlags_NoInputs)) chams::save();
				ImGui::Text("Open part - colour"); ImGui::SameLine(ImGui::GetWindowWidth()-150);
				AlexaToggle("stopen_tog", &s.stroke_open); ImGui::SameLine();
				if (s.stroke_open) if (ImGui::ColorEdit3("##stopen", s.stroke_open_color, ImGuiColorEditFlags_NoInputs)) chams::save();
				EndSectionV2();
			}
		}
	}
	// ---------- 06 Wire ----------
	{
		bool isSearchWire = g_search_buf[0]!='\0';
		bool matches_wire = SectionMatches("Wireframe (3D)", "3D wireframe", "Wireframe Wire size Wire strength Wire through walls Wire open");
		bool show = !isSearchWire || matches_wire;
		if (show) {
			bool forced = isSearchWire && matches_wire;
			if (BeginSectionV2("wire", "06", "Wireframe (3D)", "3D wireframe overlay", &s.wire, forced)) {
				if (ImGui::SliderInt("Wire size (px)", &s.wire_size, 1, 8)) chams::save();
				if (ImGui::SliderFloat("Wire strength##walpha", &s.wire_alpha, 0.05f, 1.f, "%.2f")) chams::save();
				if (ImGui::ColorEdit3("Wire through walls", s.wire_through_color, ImGuiColorEditFlags_NoInputs)) chams::save();
				if (ImGui::Checkbox("Wire open part##wireopen", &s.wire_open)) chams::save();
				if (s.wire_open) if (ImGui::ColorEdit3("Wire open##wireopen2", s.wire_open_color, ImGuiColorEditFlags_NoInputs)) chams::save();
				EndSectionV2();
			}
		}
	}
	// ---------- 07 Third person ----------
	{
		bool isSearch = g_search_buf[0]!='\0';
		bool matches = SectionMatches("Third person", "My own character", "Third person My own character No chams on my own character Safety cap own distance hide_own");
		if (matches) {
			bool forced = isSearch && matches;
			if (BeginSectionV2("own", "07", "Third person — My own character", "Prevent self-chams in third person view", nullptr, forced)) {
				if (ImGui::Checkbox("No chams on my own character (auto)##hideown", &s.hide_own)) chams::save();
				if (s.hide_own) {
					ImGui::TextDisabled("Closest draw with stable 2.4-2.6 m distance is considered self");
					if (ImGui::SliderFloat("Safety cap: nearer than##owncap", &s.own_distance, 0.5f, 15.f, "%.1f m")) chams::save();
					ImGui::TextDisabled("Further than this is never treated as mine");
				}
				EndSectionV2();
			}
		}
	}
	// ---------- 08 FPS ----------
	{
		bool isSearch = g_search_buf[0]!='\0';
		bool matches = SectionMatches("FPS Counter", "fixed — always show", "FPS Counter OSD text size OSD text color OSD position Show FPS fps fps_scale show_fps");
		if (matches) {
			bool forced = isSearch && matches;
			// FPS has toggle for always-on
			bool fpsOn = (_show_fps == 1);
			// We need to handle toggle inside header; pass pointer to temp, then apply
			bool fpsToggle = fpsOn;
			if (BeginSectionV2("fps", "08", "FPS Counter", "fixed — always show (was overlay-only)", &fpsToggle, forced)) {
				if (fpsToggle != fpsOn) { _show_fps = fpsToggle ? 1 : 0; save_config(); }
				ImGui::TextDisabled("Turned on but not showing? Now defaults to Always (1) not Overlay-only (2)");
				ImGui::Spacing();
				ImGui::Text("OSD text size"); ImGui::SameLine(ImGui::GetWindowWidth()-60); ImGui::TextDisabled("%.1f", _fps_scale);
				if (ImGui::SliderFloat("##fpsscale", &_fps_scale, 0.2f, 2.5f, "")) save_config();
				if (ImGui::ColorEdit4("OSD text color##fpscol", _fps_col, ImGuiColorEditFlags_AlphaBar)) save_config();
				const char* posItems[] = {"Top left","Top right","Bottom left","Bottom right"};
				int pos = (int)_fps_pos;
				if (ImGui::Combo("OSD position##fpspos", &pos, posItems, 4)) { _fps_pos = pos; save_config(); }
				ImGui::TextDisabled("Press assigned FPS key to toggle • also in Settings > Overlay");
				EndSectionV2();
			} else {
				if (fpsToggle != fpsOn) { _show_fps = fpsToggle ? 1 : 0; save_config(); }
			}
		}
	}
	// ---------- 09 Logs ----------
	{
		bool isSearch = g_search_buf[0]!='\0';
		bool matches = SectionMatches("Diagnostics & Logs", "Diagnostics & Logs", "Diagnostics Logs dump Refresh logs Open log folder chams-dump.txt");
		if (matches) {
			bool forced = isSearch && matches;
			if (BeginSectionV2("logs", "09", "Diagnostics & Logs", "Diagnostic trace for support", nullptr, forced)) {
				static std::vector<std::string> s_log_lines; static bool loaded=false;
				if(!loaded){ std::ifstream f(chams::dump_path()); if(f.is_open()){ std::string l; while(std::getline(f,l)) s_log_lines.push_back(l);} loaded=true; }
				if (ImGui::Button("Refresh logs")) { s_log_lines.clear(); loaded=false; }
				ImGui::SameLine(); if (ImGui::Button("Open log folder")) { char p[MAX_PATH*4]={}; GetTempPathA(sizeof(p),p); std::string c="explorer \""+std::string(p)+"\""; system(c.c_str()); }
				ImGui::BeginChild("chamslogscroll", ImVec2(0,120), true);
				int start = (int)s_log_lines.size()>20 ? (int)s_log_lines.size()-20 : 0;
				for(int i=start;i<(int)s_log_lines.size();++i) ImGui::TextColored(ImVec4(0.8f,0.8f,0.8f,1.f), "%s", s_log_lines[i].c_str());
				ImGui::EndChild();
				EndSectionV2();
			}
		}
	}

	if (ImGui::Button("Reset to defaults")) { s = chams::settings(); _show_fps=1; chams::save(); save_config(); }
	ImGui::SameLine(); ImGui::TextDisabled("created by hazu@alexainc  •  Graphics Enhancer");
	ImGui::SameLine(); ImGui::TextDisabled(" | catalog v2 (Figma #0B0E11)");
	ImGui::PopStyleColor(3);
	ImGui::PopItemWidth();
}

#endif
