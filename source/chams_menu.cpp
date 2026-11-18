/*
 * chams_menu.cpp -- 1:1 Figma replica (836×3356 ReShade/CHAMS)
 * Header: "Chams menu by AlexaInc"  Footer: "hazu @ alexainc"
 * Palette: #0B0E11 page, #11171D card/header, #192128 inner, #28323B line, #94E8C1 mint, #F09CAD pink, #EBF0F3 text, #97A6B2 dim, #687985 muted
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
// ---------- palette ----------
static const ImVec4 kPage     = ImVec4(0x0B/255.f,0x0E/255.f,0x11/255.f,1.f);
static const ImVec4 kCard     = ImVec4(0x11/255.f,0x17/255.f,0x1D/255.f,1.f);
static const ImVec4 kInner    = ImVec4(0x19/255.f,0x21/255.f,0x28/255.f,1.f);
static const ImVec4 kLine     = ImVec4(0x28/255.f,0x32/255.f,0x3B/255.f,1.f);
static const ImVec4 kText     = ImVec4(0xEB/255.f,0xF0/255.f,0xF3/255.f,1.f);
static const ImVec4 kDim      = ImVec4(0x97/255.f,0xA6/255.f,0xB2/255.f,1.f);
static const ImVec4 kMuted    = ImVec4(0x68/255.f,0x79/255.f,0x85/255.f,1.f);
static const ImVec4 kMint     = ImVec4(0x94/255.f,0xE8/255.f,0xC1/255.f,1.f);
static const ImVec4 kMintDark = ImVec4(0x20/255.f,0x3D/255.f,0x34/255.f,1.f);
static const ImVec4 kPink     = ImVec4(0xF0/255.f,0x9C/255.f,0xAD/255.f,1.f);
static const ImVec4 kCream    = ImVec4(1.f,1.f,200/255.f,1.f);
static std::unordered_map<std::string,bool> g_open = {
  {"runtime",true},{"general",true},{"third",true},{"fps",true},{"glow",true},{"stroke",true},{"wire",true},{"logs",false}
};
static float Unit(){ return ImGui::GetFontSize()/13.f; }
static ImU32 Col(const ImVec4 &c){ return ImGui::GetColorU32(c); }
static float Hair(){ return std::max(1.f, std::floor(Unit())); }
static ImVec4 Mix(const ImVec4 &a,const ImVec4 &b,float t){ return ImVec4(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t); }
static float Rounding(){ return 8.f*Unit(); }
bool AlexaToggle(const char* id,bool* v){
  ImVec2 sz(32,18); ImGui::InvisibleButton(id,sz);
  bool hovered=ImGui::IsItemHovered(); bool clicked=ImGui::IsItemClicked();
  if(clicked) *v=!*v;
  ImDrawList* dl=ImGui::GetWindowDrawList();
  ImVec2 p0=ImGui::GetItemRectMin(); ImVec2 p1={p0.x+sz.x,p0.y+sz.y}; float r=sz.y*0.5f;
  ImVec4 bg=*v?kMint:ImVec4(0.10f,0.13f,0.16f,1.f);
  if(hovered) bg=Mix(bg,kText,0.06f);
  dl->AddRectFilled(p0,p1,Col(bg),r);
  if(!*v) dl->AddRect(p0,p1,Col(kLine),r,0,Hair());
  float kr=6.f; float kx=*v?(p1.x-kr-2.f):(p0.x+kr+2.f); float ky=p0.y+sz.y*0.5f;
  dl->AddCircleFilled(ImVec2(kx,ky),kr,Col(ImVec4(0.08f,0.10f,0.12f,1.f)),16);
  if(!*v) dl->AddCircleFilled(ImVec2(kx,ky),kr-0.5f,Col(kMuted),16);
  return clicked;
}
void Chevron(ImDrawList* dl,ImVec2 c,bool open,ImVec4 col4){
  float k=Unit(); float s=5.f*k; ImU32 col=Col(col4);
  if(open){ dl->AddLine(ImVec2(c.x-s*0.45f,c.y-s*0.2f),ImVec2(c.x,c.y+s*0.3f),col,1.6f*k); dl->AddLine(ImVec2(c.x+s*0.45f,c.y-s*0.2f),ImVec2(c.x,c.y+s*0.3f),col,1.6f*k); }
  else{ dl->AddLine(ImVec2(c.x-s*0.2f,c.y-s*0.45f),ImVec2(c.x+s*0.3f,c.y),col,1.6f*k); dl->AddLine(ImVec2(c.x-s*0.2f,c.y+s*0.45f),ImVec2(c.x+s*0.3f,c.y),col,1.6f*k); }
}
bool BeginFigmaSection(const char* id,const char* num,const char* title,const char* subtitle,bool* tog){
  float k=Unit(); bool &open=g_open[id]; bool eff=open;
  ImVec2 cur=ImGui::GetCursorScreenPos(); float w=ImGui::GetContentRegionAvail().x;
  float headerH=ImGui::GetFrameHeight()+10.f*k; if(subtitle&&subtitle[0]) headerH+=14.f*k;
  float r=Rounding();
  ImGui::PushID(id);
  bool headerPressed=ImGui::InvisibleButton("##hdr",ImVec2(w,headerH));
  bool hovered=ImGui::IsItemHovered(); bool held=ImGui::IsItemActive();
  if(headerPressed){
    ImVec2 mp=ImGui::GetIO().MousePos; bool overTog=false;
    if(tog){ float tl=cur.x+w-60*k; if(mp.x>=tl&&mp.x<=tl+32&&mp.y>=cur.y&&mp.y<=cur.y+headerH) overTog=true; }
    if(!overTog) open=!open; eff=open;
  }
  ImDrawList* dl=ImGui::GetWindowDrawList();
  ImVec4 hdrCol=held?Mix(kCard,kText,0.04f):hovered?Mix(kCard,kText,0.03f):kCard;
  ImDrawFlags hc=eff?ImDrawFlags_RoundCornersTop:ImDrawFlags_RoundCornersAll;
  dl->AddRectFilled(cur,ImVec2(cur.x+w,cur.y+headerH),Col(hdrCol),r,hc);
  dl->AddRect(cur,ImVec2(cur.x+w,cur.y+headerH),Col(kLine),r,hc,Hair());
  float pad=12.f*k; float ty=cur.y+9.f*k;
  // chevron left
  Chevron(dl,ImVec2(cur.x+pad+2,cur.y+headerH*0.5f),eff, eff?kMint:kDim);
  float tx=cur.x+pad+16.f*k;
  dl->AddText(ImVec2(tx,ty),Col(kText),title);
  if(subtitle&&subtitle[0]){ float sy=ty+ImGui::GetFontSize()+3*k; dl->AddText(ImVec2(tx,sy),Col(kDim),subtitle); }
  if(num){ ImVec2 ns=ImGui::CalcTextSize(num); dl->AddText(ImVec2(cur.x+w-10*k-ns.x,ty),Col(kDim),num); }
  // toggle on right if provided (vertically centered)
  if(tog){
    ImVec2 tp=ImVec2(cur.x+w-46.f*k - (num?0:14*k), cur.y+(headerH-18)/2.f - (subtitle?4*k:0));
    ImGui::SetCursorScreenPos(tp); ImGui::PushID("tog"); AlexaToggle("##t",tog); ImGui::PopID();
  }
  ImGui::PopID();
  ImGui::SetCursorScreenPos(ImVec2(cur.x,cur.y+headerH));
  if(!eff){ ImGui::Dummy(ImVec2(0,7*k)); return false; }
  ImGui::PushStyleColor(ImGuiCol_ChildBg,kCard);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,r);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(12*k,12*k));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,Hair());
  ImGui::BeginChild((std::string(id)+"_body").c_str(),ImVec2(w,0),true,ImGuiWindowFlags_AlwaysAutoResize);
  return true;
}
void EndFigmaSection(){ ImGui::EndChild(); ImGui::PopStyleVar(3); ImGui::PopStyleColor(1); ImGui::Dummy(ImVec2(0,7*Unit())); }
void Divider(){ ImGui::Dummy(ImVec2(0,1)); ImDrawList* dl=ImGui::GetWindowDrawList(); ImVec2 c=ImGui::GetCursorScreenPos(); float w=ImGui::GetContentRegionAvail().x; dl->AddLine(ImVec2(c.x,c.y),ImVec2(c.x+w,c.y),Col(kLine),Hair()); ImGui::Dummy(ImVec2(0,6*Unit())); }
void HeaderChamsByAlexa(){
  float k=Unit(); float w=ImGui::GetContentRegionAvail().x; float h=70;
  ImVec2 cur=ImGui::GetCursorScreenPos(); ImDrawList* dl=ImGui::GetWindowDrawList();
  dl->AddRectFilled(cur,ImVec2(cur.x+w,cur.y+h),Col(kCard));
  dl->AddLine(ImVec2(cur.x,cur.y+h),ImVec2(cur.x+w,cur.y+h),Col(kLine),Hair());
  const char* a="Chams menu"; const char* b="by"; const char* c="AlexaInc";
  float ax=cur.x+w*0.5f - (ImGui::CalcTextSize(a).x + ImGui::CalcTextSize(b).x + ImGui::CalcTextSize(c).x + 12)/2;
  float cy=cur.y+h*0.5f - ImGui::GetFontSize()*0.5f;
  ImVec2 p={ax,cy};
  dl->AddText(p,Col(kText),a); p.x+=ImGui::CalcTextSize(a).x+6;
  dl->AddText(p,Col(kDim),b); p.x+=ImGui::CalcTextSize(b).x+6;
  dl->AddText(p,Col(kMint),c);
  ImGui::SetCursorScreenPos(ImVec2(cur.x,cur.y+h));
}
void FooterHazu(){
  float k=Unit(); float w=ImGui::GetContentRegionAvail().x; float h=32;
  ImVec2 cur=ImGui::GetCursorScreenPos(); ImDrawList* dl=ImGui::GetWindowDrawList();
  dl->AddRectFilled(cur,ImVec2(cur.x+w,cur.y+h),Col(kCard));
  dl->AddLine(cur,ImVec2(cur.x+w,cur.y),Col(kLine),Hair());
  const char* a="hazu"; const char* b=" @ "; const char* d="alexainc";
  ImVec2 s1=ImGui::CalcTextSize(a), s2=ImGui::CalcTextSize(b), s3=ImGui::CalcTextSize(d);
  float tw=s1.x+s2.x+s3.x+8; float cx=cur.x+w*0.5f - tw*0.5f; float cy=cur.y+h*0.5f - s1.y*0.5f;
  dl->AddCircleFilled(ImVec2(cx-8,cy+s1.y*0.5f),3*Unit(),Col(kMint),8);
  dl->AddText(ImVec2(cx,cy),Col(kMint),a);
  dl->AddText(ImVec2(cx+s1.x,cy),Col(kDim),b);
  dl->AddText(ImVec2(cx+s1.x+s2.x,cy),Col(kText),d);
  ImGui::SetCursorScreenPos(ImVec2(cur.x,cur.y+h+6*k));
}
bool FillCard(const char* label,bool sel,const ImVec2& sz){
  ImVec4 bg=sel?kMintDark:kInner; ImVec4 bd=sel?kMint:kLine;
  ImGui::PushStyleColor(ImGuiCol_Button,bg); ImGui::PushStyleColor(ImGuiCol_ButtonHovered,Mix(bg,kText,0.06f)); ImGui::PushStyleColor(ImGuiCol_ButtonActive,Mix(bg,kMint,0.12f)); ImGui::PushStyleColor(ImGuiCol_Border,bd);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,Hair()); ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,5*Unit());
  bool clk=ImGui::Button(label,sz); ImGui::PopStyleVar(2); ImGui::PopStyleColor(4);
  if(sel){ ImDrawList* dl=ImGui::GetWindowDrawList(); ImVec2 a=ImGui::GetItemRectMin(); dl->AddCircleFilled(ImVec2(a.x+12*Unit(),a.y+sz.y*0.5f),5*Unit(),Col(kMint),12); dl->AddCircleFilled(ImVec2(a.x+12*Unit(),a.y+sz.y*0.5f),2.2f*Unit(),Col(kCard),8); }
  return clk;
}
} // anon
void reshade::runtime::draw_gui_chams(){
  static bool loaded=false; if(!loaded){ chams::load(); loaded=true; }
  chams::settings &s=chams::g_settings;
  ImGui::PushStyleColor(ImGuiCol_WindowBg,kPage);
  ImGui::PushStyleColor(ImGuiCol_Text,kText);
  ImGui::PushStyleColor(ImGuiCol_Border,kLine);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(8*Unit(),8*Unit()));
  HeaderChamsByAlexa();
  // Master switch standalone
  {
    float k=Unit(); float w=ImGui::GetContentRegionAvail().x; ImVec2 cur=ImGui::GetCursorScreenPos(); float h=74* k/(k==1?1:1); // keep compact: 74px at 1k
    h=58; // visually match Figma 74->58 in overlay
    ImDrawList* dl=ImGui::GetWindowDrawList(); float r=Rounding();
    dl->AddRectFilled(cur,ImVec2(cur.x+w,cur.y+h),Col(kCard),r);
    dl->AddRect(cur,ImVec2(cur.x+w,cur.y+h),Col(kLine),r,0,Hair());
    // left: master switch
    ImGui::SetCursorScreenPos(ImVec2(cur.x+16,cur.y+12));
    ImGui::Text("Master switch");
    ImGui::SameLine(0,10); AlexaToggle("##master", &s.enabled);
    ImGui::SameLine(0,10); ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(6,2)); ImGui::SmallButton("F8"); ImGui::PopStyleVar(1);
    ImGui::SameLine(0,8); ImGui::TextDisabled("Toggle in game");
    ImGui::SetCursorScreenPos(ImVec2(cur.x+16,cur.y+32));
    ImGui::TextColored(kMint,"Chams enabled");
    // right: default profile
    ImGui::SetCursorScreenPos(ImVec2(cur.x+w-170,cur.y+10));
    ImGui::Text("Default profile");
    ImGui::SetCursorScreenPos(ImVec2(cur.x+w-150,cur.y+28));
    ImGui::TextDisabled("chams.ini  /  CHAMS");
    ImGui::SetCursorScreenPos(ImVec2(cur.x,cur.y+h+7*k));
  }
  // Runtime capture
  {
    bool open=g_open["runtime"]; bool eff=open;
    float k=Unit(); float w=ImGui::GetContentRegionAvail().x; ImVec2 cur=ImGui::GetCursorScreenPos(); float hh= 44* k; // header
    float r=Rounding();
    ImDrawList* dl=ImGui::GetWindowDrawList();
    // header
    ImGui::PushID("runtime");
    bool hdrPressed=ImGui::InvisibleButton("##hdr_rt",ImVec2(w,hh));
    bool hov=ImGui::IsItemHovered(); bool held=ImGui::IsItemActive();
    if(hdrPressed) g_open["runtime"]=!open;
    ImVec4 hc=held?Mix(kCard,kText,0.04f):hov?Mix(kCard,kText,0.03f):kCard;
    ImDrawFlags hflg=eff?ImDrawFlags_RoundCornersTop:ImDrawFlags_RoundCornersAll;
    dl->AddRectFilled(cur,ImVec2(cur.x+w,cur.y+hh),Col(hc),r,hflg);
    dl->AddRect(cur,ImVec2(cur.x+w,cur.y+hh),Col(kLine),r,hflg,Hair());
    Chevron(dl,ImVec2(cur.x+14,cur.y+hh*0.5f),eff,eff?kMint:kDim);
    dl->AddText(ImVec2(cur.x+28,cur.y+11),Col(kText),"Runtime capture");
    // badge experimental
    dl->AddRectFilled(ImVec2(cur.x+150,cur.y+8),ImVec2(cur.x+240,cur.y+hh-8),Col(kInner),4);
    { ImVec2 bs=ImGui::CalcTextSize("EXPERIMENTAL"); dl->AddText(ImVec2(cur.x+150+ (90-bs.x)*0.5f,cur.y+12),Col(kDim),"EXPERIMENTAL"); }
    if(eff){
      // buttons right
      float bw1= ImGui::CalcTextSize("Start tracking").x+28; float bw2= ImGui::CalcTextSize("Add marker").x+28;
      // we draw buttons via ImGui at right positions
      ImGui::SetCursorScreenPos(ImVec2(cur.x+w-bw1-bw2-16,cur.y+7));
      if(ImGui::Button("Start tracking",ImVec2(bw1,26))){ }
      ImGui::SameLine(0,8);
      if(ImGui::Button("Add marker",ImVec2(bw2,26))){ }
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(cur.x,cur.y+hh));
    if(eff){
      ImGui::PushStyleColor(ImGuiCol_ChildBg,kCard);
      ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,r);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(12*k,10*k));
      ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,Hair());
      ImGui::BeginChild("runtime_body",ImVec2(w,0),true,ImGuiWindowFlags_AlwaysAutoResize);
      ImGui::TextDisabled("Tracking stopped");
      ImGui::SameLine(0,24); ImGui::TextDisabled("Frames 0"); ImGui::SameLine(0,12); ImGui::TextDisabled("Contexts 0"); ImGui::SameLine(0,12); ImGui::TextDisabled("Draw records 0"); ImGui::SameLine(0,12); ImGui::TextDisabled("Dropped 0");
      Divider();
      // Dump mode row
      if(ImGui::Checkbox("Dump mode##dm",&s.dump)) chams::save();
      ImGui::SameLine(0,16); ImGui::TextDisabled("Write draw info to ReShade.log · 2,000 lines / activation");
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Copy dump path").x - 24,0);
      if(ImGui::Button("Copy dump path##cdp")){ ImGui::OpenPopup("copied"); }
      if(ImGui::BeginPopup("copied")){ ImGui::TextDisabled("Copied"); ImGui::EndPopup(); }
      if(ImGui::Checkbox("Dump ALL draws##dad",&s.dump_all)) chams::save();
      ImGui::SameLine(0,16); ImGui::TextDisabled("Find missing pieces · 3,000 draws");
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Experimental diagnostics").x - 8,0);
      ImGui::TextDisabled("Experimental diagnostics");
      ImGui::EndChild(); ImGui::PopStyleVar(3); ImGui::PopStyleColor(1);
      ImGui::Dummy(ImVec2(0,7*k));
    } else ImGui::Dummy(ImVec2(0,7*k));
  }
  // CATEGORIES bar
  {
    ImGui::TextColored(kMint,"CATEGORIES");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("6 panels · expanded").x,0);
    int expanded=0; for(auto &kv:g_open) if(kv.second) expanded++; // includes runtime but figma counts 6 panels (excluding master/runtime/logs?) Keep 6 label but update
    ImGui::TextDisabled("%d panels · expanded", expanded>6?6:expanded);
    ImGui::Dummy(ImVec2(0,4*Unit()));
  }
  // 01 General
  if(BeginFigmaSection("general","01","General","Choose how characters are detected and rendered.",nullptr)){
    // Render mode
    ImGui::Text("Render mode"); ImGui::TextDisabled("Per-draw passes, depth tested by the game. Safe default.");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 294,0);
    ImGui::SetNextItemWidth(294);
    const char* rm_items[]={"Direct (recommended)","Overlay (experimental)"};
    // map: 1=Direct, 0=Overlay in settings
    int rm_sel = (s.render_mode==1)?0:1;
    if(ImGui::Combo("##rendermode",&rm_sel, "Direct (recommended)\0Overlay (experimental)\0")){ s.render_mode = (rm_sel==0)?1:0; chams::save(); }
    Divider();
    ImGui::Text("Character detection mode");
    ImGui::TextDisabled("Wide checks 8 avatar-only uniforms for skins, cloth and hair.");
    ImGui::TextDisabled("Strict · original DLL rule    Skinned · bone matrices    All · mixed detection");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 294,0);
    ImGui::SetNextItemWidth(294);
    if(ImGui::Combo("##detectmode",&s.detect_mode,"Strict\0Wide (default)\0Skinned\0All\0")) chams::save();
    Divider();
    // Toggles row style similar to HTML: flex space-between
    ImGui::Text("See enemies through walls"); ImGui::TextDisabled("Includes glue / ice walls. Off: only occluded parts are highlighted.");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 32,0); AlexaToggle("##through",&s.through_walls);
    if(!s.through_walls) { /* trigger save on toggle helper inside */ chams::save(); }
    Divider();
    ImGui::Text("Also paint hair / cloth shaders"); ImGui::TextDisabled("Wide match uses _BackLightDir instead of _CharaLightIntensity.");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 32,0); if(AlexaToggle("##wide",&s.wide_match)) chams::save();
    ImGui::Dummy(ImVec2(0,6*Unit()));
    ImGui::TextDisabled("Overlay is experimental: separate mask buffer paints on top of the finished frame.");
    ImGui::TextColored(kMint,"Black screen? Switch to Direct, or toggle F8 off and on.");
    EndFigmaSection();
  }
  // 02 Third person
  if(BeginFigmaSection("third","02","Third person","Keep your own character clear without losing nearby enemies.",nullptr)){
    ImGui::Text("No chams on my own character"); ImGui::TextDisabled("Automatically excludes the closest draw with a stable distance of 2.4–2.6 m.");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 32,0); if(AlexaToggle("##hideown",&s.hide_own)) chams::save();
    Divider();
    ImGui::Text("Safe cap for my character"); ImGui::SameLine(ImGui::GetContentRegionAvail().x - 102,0);
    char buf[32]; snprintf(buf,sizeof(buf),"%.1f m",s.own_distance); ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(8,4)); ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,5*Unit());
    ImGui::PushStyleColor(ImGuiCol_FrameBg,kInner); ImGui::TextDisabled("%s",buf); // mimic badge
    ImGui::PopStyleColor(1); ImGui::PopStyleVar(2);
    // slider
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if(ImGui::SliderFloat("##owncap",&s.own_distance,0.5f,15.f,"")) chams::save();
    { float w=ImGui::GetContentRegionAvail().x; ImGui::TextDisabled("0.5 m"); ImGui::SameLine(w - ImGui::CalcTextSize("15.0 m").x,0); ImGui::TextDisabled("15.0 m"); }
    // protect
    ImVec2 cur=ImGui::GetCursorScreenPos(); float w2=ImGui::GetContentRegionAvail().x;
    ImDrawList* dl=ImGui::GetWindowDrawList(); dl->AddRectFilled(cur,ImVec2(cur.x+w2,cur.y+46),Col(kInner),7);
    ImGui::SetCursorScreenPos(ImVec2(cur.x+12,cur.y+8)); ImGui::TextColored(kMint,"Protect close enemies");
    ImGui::SetCursorScreenPos(ImVec2(cur.x+12,cur.y+22)); ImGui::TextDisabled("A draw farther than the safe cap is never treated as your own character.");
    ImGui::SetCursorScreenPos(ImVec2(cur.x,cur.y+52));
    ImGui::TextDisabled("AUTO DETECTION"); ImGui::TextDisabled("Stable distance + nearest draw");
    EndFigmaSection();
  }
  // 03 FPS counter
  if(BeginFigmaSection("fps","03","FPS counter","A lightweight performance overlay, positioned your way.",nullptr)){
    ImGui::Text("Show FPS counter"); ImGui::TextDisabled("Off by default. Uses the existing overlay GL guard to prevent flicker.");
    bool fpsOn = (_show_fps!=0);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 32,0);
    if(AlexaToggle("##fpsshow",&fpsOn)){ _show_fps=fpsOn?1:0; save_config(); }
    Divider();
    ImGui::Text("Position"); ImGui::TextDisabled("Top left  /  Top right  /  Bottom left  /  Bottom right");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 294,0);
    ImGui::SetNextItemWidth(294);
    int pos=(int)_fps_pos; const char* pos_items="Top left\0Top right\0Bottom left\0Bottom right\0";
    if(ImGui::Combo("##fpspos",&pos,pos_items)) { _fps_pos=pos; save_config(); }
    Divider();
    ImGui::Text("Scale"); ImGui::SameLine(ImGui::GetContentRegionAvail().x - 102,0); { char b[16]; snprintf(b,sizeof(b),"%.1fx",_fps_scale); ImGui::TextDisabled("%s",b); }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if(ImGui::SliderFloat("##fpsscale",&_fps_scale,0.2f,2.5f,"")) save_config();
    { float w=ImGui::GetContentRegionAvail().x; ImGui::TextDisabled("0.2×"); ImGui::SameLine(w - ImGui::CalcTextSize("2.5×").x,0); ImGui::TextDisabled("2.5×"); }
    Divider();
    ImGui::Text("Color · RGBA");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 294,0);
    // color dropdown mimic: show swatch + hex + alpha
    {
      ImGui::PushStyleColor(ImGuiCol_FrameBg,kInner); ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,5*Unit());
      ImGui::SetNextItemWidth(294);
      char hex[64]; snprintf(hex,sizeof(hex),"#%02X%02X%02X / %d%%", (int)(_fps_col[0]*255),(int)(_fps_col[1]*255),(int)(_fps_col[2]*255),(int)(_fps_col[3]*100));
      // use color edit with swatch
      ImVec2 cur2=ImGui::GetCursorScreenPos(); float cw=294;
      ImDrawList* dl2=ImGui::GetWindowDrawList();
      dl2->AddRectFilled(cur2,ImVec2(cur2.x+cw,cur2.y+28),Col(kInner),5);
      ImU32 sw=IM_COL32((int)(_fps_col[0]*255),(int)(_fps_col[1]*255),(int)(_fps_col[2]*255),255);
      dl2->AddRectFilled(ImVec2(cur2.x+6,cur2.y+5),ImVec2(cur2.x+26,cur2.y+23),sw,4);
      dl2->AddText(ImVec2(cur2.x+36,cur2.y+7),Col(kText),hex);
      dl2->AddText(ImVec2(cur2.x+cw-16,cur2.y+7),Col(kDim),"∨");
      ImGui::Dummy(ImVec2(cw,28));
      if(ImGui::IsItemClicked()) ImGui::OpenPopup("fpscolorpick");
      if(ImGui::BeginPopup("fpscolorpick")){
        if(ImGui::ColorEdit4("##fpscol",_fps_col, ImGuiColorEditFlags_AlphaBar)) save_config();
        ImGui::EndPopup();
      }
      ImGui::PopStyleVar(1); ImGui::PopStyleColor(1);
    }
    ImGui::Dummy(ImVec2(0,6*Unit()));
    ImGui::TextDisabled("Saved in chams.ini [CHAMS] as ShowFPS and FPSPosition.");
    ImGui::TextDisabled("Corner placement automatically adapts when the viewport is resized.");
    EndFigmaSection();
  }
  // 04 Glow & fill
  if(BeginFigmaSection("glow","04","Glow & fill","Control body fill, clothing detail and visible / hidden colors.",&s.glow)){
    ImGui::Text("Player highlight"); // keep toggle in header already; this duplicate for 1:1 but header has it
    // Fill mode grid
    ImGui::TextDisabled("Fill mode");
    ImVec2 avail=ImGui::GetContentRegionAvail(); float bw=(avail.x-8)*0.5f; ImVec2 bsz(bw,34);
    bool c0=s.glow_mode==0,c1=s.glow_mode==1,c2=s.glow_mode==2,c3=s.glow_mode==3;
    if(FillCard(" Solid colour",c0,bsz)&&!c0){ s.glow_mode=0; chams::save(); } ImGui::SameLine();
    if(FillCard(" Blend",c1,bsz)&&!c1){ s.glow_mode=1; chams::save(); }
    if(FillCard(" Real clothes + light colour",c2,bsz)&&!c2){ s.glow_mode=2; chams::save(); } ImGui::SameLine();
    if(FillCard(" Real clothes only",c3,bsz)&&!c3){ s.glow_mode=3; chams::save(); }
    if(s.glow_mode==1) ImGui::TextDisabled("Blend preserves real clothes and layers colour over them.");
    else if(s.glow_mode==0) ImGui::TextDisabled("Whole body filled with solid colour — classic chams.");
    else ImGui::TextDisabled("Real clothes drawn through walls — best for outfit visibility.");
    Divider();
    ImGui::Text("Glow strength"); ImGui::SameLine(ImGui::GetContentRegionAvail().x - 102,0); { char b[16]; snprintf(b,sizeof(b),"%.2f",s.glow_alpha); ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,5*Unit()); ImGui::PushStyleColor(ImGuiCol_FrameBg,kInner); ImGui::Button(b,ImVec2(102,28)); ImGui::PopStyleColor(1); ImGui::PopStyleVar(1); }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if(ImGui::SliderFloat("##glowalpha",&s.glow_alpha,0.05f,1.f,"")) chams::save();
    { float w=ImGui::GetContentRegionAvail().x; ImGui::TextDisabled("0.05"); ImGui::SameLine(w - ImGui::CalcTextSize("1.00").x,0); ImGui::TextDisabled("1.00"); }
    Divider();
    // Through walls colour dropdown
    ImGui::Text("Through walls · colour");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 294,0);
    {
      ImVec2 cur2=ImGui::GetCursorScreenPos(); float cw=294; ImDrawList* dl2=ImGui::GetWindowDrawList();
      dl2->AddRectFilled(cur2,ImVec2(cur2.x+cw,cur2.y+32),Col(kInner),5);
      ImU32 sw=IM_COL32((int)(s.glow_through_color[0]*255),(int)(s.glow_through_color[1]*255),(int)(s.glow_through_color[2]*255),255);
      dl2->AddRectFilled(ImVec2(cur2.x+8,cur2.y+6),ImVec2(cur2.x+28,cur2.y+26),sw,4);
      char hx[16]; snprintf(hx,sizeof(hx),"#%02X%02X%02X",(int)(s.glow_through_color[0]*255),(int)(s.glow_through_color[1]*255),(int)(s.glow_through_color[2]*255));
      dl2->AddText(ImVec2(cur2.x+36,cur2.y+8),Col(kText),hx);
      dl2->AddText(ImVec2(cur2.x+cw-16,cur2.y+8),Col(kDim),"∨");
      ImGui::Dummy(ImVec2(cw,32));
      if(ImGui::IsItemClicked()) ImGui::OpenPopup("glowthroughpick");
      if(ImGui::BeginPopup("glowthroughpick")){ if(ImGui::ColorEdit3("##glowthrough",s.glow_through_color,ImGuiColorEditFlags_NoInputs)) chams::save(); ImGui::EndPopup(); }
    }
    // Open part
    ImGui::Text("Open part · colour");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 294 - 48,0);
    AlexaToggle("##glowopen",&s.glow_open);
    ImGui::SameLine(0,12);
    {
      ImVec2 cur2=ImGui::GetCursorScreenPos(); float cw=218; ImDrawList* dl2=ImGui::GetWindowDrawList();
      dl2->AddRectFilled(cur2,ImVec2(cur2.x+cw,cur2.y+32),Col(kInner),5);
      ImU32 sw=IM_COL32((int)(s.glow_open_color[0]*255),(int)(s.glow_open_color[1]*255),(int)(s.glow_open_color[2]*255),255);
      dl2->AddRectFilled(ImVec2(cur2.x+8,cur2.y+6),ImVec2(cur2.x+28,cur2.y+26),sw,4);
      char hx[16]; snprintf(hx,sizeof(hx),"#%02X%02X%02X",(int)(s.glow_open_color[0]*255),(int)(s.glow_open_color[1]*255),(int)(s.glow_open_color[2]*255));
      dl2->AddText(ImVec2(cur2.x+36,cur2.y+8),Col(kText),hx);
      dl2->AddText(ImVec2(cur2.x+cw-16,cur2.y+8),Col(kDim),"∨");
      ImGui::Dummy(ImVec2(cw,32));
      if(ImGui::IsItemClicked()) ImGui::OpenPopup("glowopenpick");
      if(ImGui::BeginPopup("glowopenpick")){ if(ImGui::ColorEdit3("##glowopen",s.glow_open_color,ImGuiColorEditFlags_NoInputs)) chams::save(); ImGui::EndPopup(); }
    }
    EndFigmaSection();
  }
  // 05 2D stroke
  if(BeginFigmaSection("stroke","05","2D stroke","A clean screen-space outline around detected characters.",&s.stroke)){
    // already toggled via header
    // Stroke size open
    ImGui::Text("Stroke size · open area"); ImGui::SameLine(ImGui::GetContentRegionAvail().x - 102,0); { char b[16]; snprintf(b,sizeof(b),"%d px",s.stroke_open_size); ImGui::Button(b,ImVec2(102,28)); }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if(ImGui::SliderInt("##stopen",&s.stroke_open_size,1,16)) chams::save();
    { float w=ImGui::GetContentRegionAvail().x; ImGui::TextDisabled("1 px"); ImGui::SameLine(w - ImGui::CalcTextSize("16 px").x,0); ImGui::TextDisabled("16 px"); }
    ImGui::Text("Stroke size · covered area"); ImGui::SameLine(ImGui::GetContentRegionAvail().x - 102,0); { char b2[16]; snprintf(b2,sizeof(b2),"%d px",s.stroke_through_size); ImGui::Button(b2,ImVec2(102,28)); }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if(ImGui::SliderInt("##stthrough",&s.stroke_through_size,1,16)) chams::save();
    { float w=ImGui::GetContentRegionAvail().x; ImGui::TextDisabled("1 px"); ImGui::SameLine(w - ImGui::CalcTextSize("16 px").x,0); ImGui::TextDisabled("16 px"); }
    ImGui::Text("Stroke strength"); ImGui::SameLine(ImGui::GetContentRegionAvail().x - 102,0); { char b[16]; snprintf(b,sizeof(b),"%.2f",s.stroke_alpha); ImGui::Button(b,ImVec2(102,28)); }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if(ImGui::SliderFloat("##stalpha",&s.stroke_alpha,0.05f,1.f,"")) chams::save();
    { float w=ImGui::GetContentRegionAvail().x; ImGui::TextDisabled("0.05"); ImGui::SameLine(w - ImGui::CalcTextSize("1.00").x,0); ImGui::TextDisabled("1.00"); }
    Divider();
    ImGui::Text("Open part · colour"); ImGui::SameLine(ImGui::GetContentRegionAvail().x - 294 - 48,0); AlexaToggle("##stopen_t",&s.stroke_open);
    ImGui::SameLine(0,12);
    {
      ImVec2 cur2=ImGui::GetCursorScreenPos(); float cw=218; ImDrawList* dl2=ImGui::GetWindowDrawList();
      dl2->AddRectFilled(cur2,ImVec2(cur2.x+cw,cur2.y+32),Col(kInner),5);
      ImU32 sw=IM_COL32((int)(s.stroke_open_color[0]*255),(int)(s.stroke_open_color[1]*255),(int)(s.stroke_open_color[2]*255),255);
      dl2->AddRectFilled(ImVec2(cur2.x+8,cur2.y+6),ImVec2(cur2.x+28,cur2.y+26),sw,4);
      char hx[16]; snprintf(hx,sizeof(hx),"#%02X%02X%02X",(int)(s.stroke_open_color[0]*255),(int)(s.stroke_open_color[1]*255),(int)(s.stroke_open_color[2]*255));
      dl2->AddText(ImVec2(cur2.x+36,cur2.y+8),Col(kText),hx);
      dl2->AddText(ImVec2(cur2.x+cw-16,cur2.y+8),Col(kDim),"∨");
      ImGui::Dummy(ImVec2(cw,32));
      if(ImGui::IsItemClicked()) ImGui::OpenPopup("stopenpick");
      if(ImGui::BeginPopup("stopenpick")){ if(ImGui::ColorEdit3("##stopen",s.stroke_open_color,ImGuiColorEditFlags_NoInputs)) chams::save(); ImGui::EndPopup(); }
    }
    ImGui::Text("Through walls · colour");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 218,0);
    {
      ImVec2 cur2=ImGui::GetCursorScreenPos(); float cw=218; ImDrawList* dl2=ImGui::GetWindowDrawList();
      dl2->AddRectFilled(cur2,ImVec2(cur2.x+cw,cur2.y+32),Col(kInner),5);
      ImU32 sw=IM_COL32((int)(s.stroke_through_color[0]*255),(int)(s.stroke_through_color[1]*255),(int)(s.stroke_through_color[2]*255),255);
      dl2->AddRectFilled(ImVec2(cur2.x+8,cur2.y+6),ImVec2(cur2.x+28,cur2.y+26),sw,4);
      char hx[16]; snprintf(hx,sizeof(hx),"#%02X%02X%02X",(int)(s.stroke_through_color[0]*255),(int)(s.stroke_through_color[1]*255),(int)(s.stroke_through_color[2]*255));
      dl2->AddText(ImVec2(cur2.x+36,cur2.y+8),Col(kText),hx);
      dl2->AddText(ImVec2(cur2.x+cw-16,cur2.y+8),Col(kDim),"∨");
      ImGui::Dummy(ImVec2(cw,32));
      if(ImGui::IsItemClicked()) ImGui::OpenPopup("stthroughpick");
      if(ImGui::BeginPopup("stthroughpick")){ if(ImGui::ColorEdit3("##stthrough",s.stroke_through_color,ImGuiColorEditFlags_NoInputs)) chams::save(); ImGui::EndPopup(); }
    }
    EndFigmaSection();
  }
  // 06 3D wireframe
  if(BeginFigmaSection("wire","06","3D wireframe","Trace character geometry with a fine, three-dimensional mesh.",nullptr)){
    // header toggle separate row inside
    ImGui::Text("3D wireframe stroke"); ImGui::SameLine(ImGui::GetContentRegionAvail().x - 32,0); if(AlexaToggle("##wire",&s.wire)) chams::save();
    Divider();
    ImGui::Text("Wire size"); ImGui::SameLine(ImGui::GetContentRegionAvail().x - 102,0); { char b[16]; snprintf(b,sizeof(b),"%d px",s.wire_size); ImGui::Button(b,ImVec2(102,28)); }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if(ImGui::SliderInt("##wsize",&s.wire_size,1,8)) chams::save();
    { float w=ImGui::GetContentRegionAvail().x; ImGui::TextDisabled("1 px"); ImGui::SameLine(w - ImGui::CalcTextSize("8 px").x,0); ImGui::TextDisabled("8 px"); }
    ImGui::Text("Wire strength"); ImGui::SameLine(ImGui::GetContentRegionAvail().x - 102,0); { char b2[16]; snprintf(b2,sizeof(b2),"%.2f",s.wire_alpha); ImGui::Button(b2,ImVec2(102,28)); }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if(ImGui::SliderFloat("##walpha",&s.wire_alpha,0.05f,1.f,"")) chams::save();
    { float w=ImGui::GetContentRegionAvail().x; ImGui::TextDisabled("0.05"); ImGui::SameLine(w - ImGui::CalcTextSize("1.00").x,0); ImGui::TextDisabled("1.00"); }
    Divider();
    ImGui::Text("Wire through walls · colour");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 294,0);
    {
      ImVec2 cur2=ImGui::GetCursorScreenPos(); float cw=294; ImDrawList* dl2=ImGui::GetWindowDrawList();
      dl2->AddRectFilled(cur2,ImVec2(cur2.x+cw,cur2.y+32),Col(kInner),5);
      ImU32 sw=IM_COL32((int)(s.wire_through_color[0]*255),(int)(s.wire_through_color[1]*255),(int)(s.wire_through_color[2]*255),255);
      dl2->AddRectFilled(ImVec2(cur2.x+8,cur2.y+6),ImVec2(cur2.x+28,cur2.y+26),sw,4);
      char hx[16]; snprintf(hx,sizeof(hx),"#%02X%02X%02X",(int)(s.wire_through_color[0]*255),(int)(s.wire_through_color[1]*255),(int)(s.wire_through_color[2]*255));
      dl2->AddText(ImVec2(cur2.x+36,cur2.y+8),Col(kText),hx);
      dl2->AddText(ImVec2(cur2.x+cw-16,cur2.y+8),Col(kDim),"∨");
      ImGui::Dummy(ImVec2(cw,32));
      if(ImGui::IsItemClicked()) ImGui::OpenPopup("wirethroughpick");
      if(ImGui::BeginPopup("wirethroughpick")){ if(ImGui::ColorEdit3("##wirethrough",s.wire_through_color,ImGuiColorEditFlags_NoInputs)) chams::save(); ImGui::EndPopup(); }
    }
    ImGui::Text("Wire open part · colour"); ImGui::SameLine(ImGui::GetContentRegionAvail().x - 294 - 48,0); AlexaToggle("##wireopen",&s.wire_open);
    ImGui::SameLine(0,12);
    {
      ImVec2 cur2=ImGui::GetCursorScreenPos(); float cw=218; ImDrawList* dl2=ImGui::GetWindowDrawList();
      dl2->AddRectFilled(cur2,ImVec2(cur2.x+cw,cur2.y+32),Col(kInner),5);
      ImU32 sw=IM_COL32((int)(s.wire_open_color[0]*255),(int)(s.wire_open_color[1]*255),(int)(s.wire_open_color[2]*255),255);
      dl2->AddRectFilled(ImVec2(cur2.x+8,cur2.y+6),ImVec2(cur2.x+28,cur2.y+26),sw,4);
      char hx[16]; snprintf(hx,sizeof(hx),"#%02X%02X%02X",(int)(s.wire_open_color[0]*255),(int)(s.wire_open_color[1]*255),(int)(s.wire_open_color[2]*255));
      dl2->AddText(ImVec2(cur2.x+36,cur2.y+8),Col(kText),hx);
      dl2->AddText(ImVec2(cur2.x+cw-16,cur2.y+8),Col(kDim),"∨");
      ImGui::Dummy(ImVec2(cw,32));
      if(ImGui::IsItemClicked()) ImGui::OpenPopup("wireopenpick");
      if(ImGui::BeginPopup("wireopenpick")){ if(ImGui::ColorEdit3("##wireopen",s.wire_open_color,ImGuiColorEditFlags_NoInputs)) chams::save(); ImGui::EndPopup(); }
    }
    // Wireframe is off banner
    if(!s.wire){
      ImVec2 cur2=ImGui::GetCursorScreenPos(); float w2=ImGui::GetContentRegionAvail().x;
      ImDrawList* dl2=ImGui::GetWindowDrawList(); dl2->AddRectFilled(cur2,ImVec2(cur2.x+w2,cur2.y+46),Col(kInner),7);
      ImGui::SetCursorScreenPos(ImVec2(cur2.x+12,cur2.y+8)); ImGui::TextColored(kMint,"Wireframe is off");
      ImGui::SetCursorScreenPos(ImVec2(cur2.x+12,cur2.y+22)); ImGui::TextDisabled("Enable 3D wireframe stroke to apply these settings.");
      ImGui::SetCursorScreenPos(ImVec2(cur2.x,cur2.y+52));
    }
    EndFigmaSection();
  }
  // Logs collapsed
  {
    bool open=g_open["logs"]; float k=Unit(); float w=ImGui::GetContentRegionAvail().x; ImVec2 cur=ImGui::GetCursorScreenPos(); float h=48;
    ImDrawList* dl=ImGui::GetWindowDrawList(); float r=Rounding();
    ImGui::PushID("logs");
    bool hdrPressed=ImGui::InvisibleButton("##hdr_logs",ImVec2(w,h));
    bool hov=ImGui::IsItemHovered(); bool held=ImGui::IsItemActive();
    if(hdrPressed) g_open["logs"]=!open;
    ImVec4 hc=held?Mix(kCard,kText,0.04f):hov?Mix(kCard,kText,0.03f):kCard;
    dl->AddRectFilled(cur,ImVec2(cur.x+w,cur.y+h),Col(hc),r);
    dl->AddRect(cur,ImVec2(cur.x+w,cur.y+h),Col(kLine),r,0,Hair());
    Chevron(dl,ImVec2(cur.x+14,cur.y+h*0.5f),open,open?kMint:kDim);
    dl->AddText(ImVec2(cur.x+28,cur.y+14),Col(kText),"Logs");
    char rec[32]; snprintf(rec,sizeof(rec),"%d records",0);
    ImVec2 rs=ImGui::CalcTextSize(rec); dl->AddText(ImVec2(cur.x+w-12-rs.x,cur.y+14),Col(kDim),rec);
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(cur.x,cur.y+h+7*k));
    if(open){
      ImGui::PushStyleColor(ImGuiCol_ChildBg,kCard);
      ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,r);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(12*k,10*k));
      ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,Hair());
      ImGui::BeginChild("logs_body",ImVec2(w,0),true,ImGuiWindowFlags_AlwaysAutoResize);
      static std::vector<std::string> s_log_lines; static bool loadedLogs=false;
      if(!loadedLogs){ std::ifstream f(chams::dump_path()); if(f.is_open()){ std::string l; while(std::getline(f,l)) s_log_lines.push_back(l);} loadedLogs=true; }
      if(ImGui::Button("Refresh logs")) { s_log_lines.clear(); loadedLogs=false; }
      ImGui::SameLine(); if(ImGui::Button("Open log folder")) { char p[MAX_PATH*4]={}; GetTempPathA(sizeof(p),p); std::string c="explorer \""+std::string(p)+"\""; system(c.c_str()); }
      ImGui::BeginChild("chamslogscroll",ImVec2(0,120),true);
      int start=(int)s_log_lines.size()>20?(int)s_log_lines.size()-20:0;
      for(int i=start;i<(int)s_log_lines.size();++i) ImGui::TextColored(ImVec4(0.8f,0.8f,0.8f,1.f),"%s",s_log_lines[i].c_str());
      ImGui::EndChild();
      ImGui::EndChild(); ImGui::PopStyleVar(3); ImGui::PopStyleColor(1);
      ImGui::Dummy(ImVec2(0,7*k));
    }
  }
  FooterHazu();
  ImGui::PopStyleVar(1);
  ImGui::PopStyleColor(3);
}
#endif
