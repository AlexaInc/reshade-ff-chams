/*
 * chams_menu.cpp -- 1:1 Figma replica (836x3356 ReShade/CHAMS)
 * Header: "Chams menu by AlexaInc"   Footer: "hazu @ alexainc"
 * Palette: #0B0E11 page, #11171D card/header, #192128 inner, #28323B line,
 *          #94E8C1 mint, #F09CAD pink, #EBF0F3 text, #97A6B2 dim, #687985 muted
 *
 * LAYOUT RULES (stage 104, fixes "text overlapping switches / buttons")
 * ---------------------------------------------------------------------
 *   Nothing is placed with a hard-coded x offset any more. Every row is built
 *   with the helpers below, so it flows and wraps by itself:
 *
 *     RowBegin / RowEnd        a row = label block (left) + control block
 *                              (right). If they do not both fit, the control
 *                              is moved to its own line instead of drawing
 *                              over the label.
 *     Label2(title, hint)      two-line label, the hint is clipped to the
 *                              space that is really left on the row.
 *     ToggleRight(id, v)      32x18 mint switch, right aligned, cannot be
 *                              overlapped by text.
 *     SliderRow(...)/Badge     label + value pill + full width slider.
 *     ValueRow(...)            label + right aligned read-only pill.
 *     ComboRow(...)            label + dropdown, full width if narrow.
 *     ColorRow(...)            label + [swatch hex alpha] picker, same rule.
 *     CheckRow(...)            checkbox + hint text (wraps, never clipped).
 *
 *   Click handling: header/body hit boxes are registered once, popups are
 *   opened on mouse RELEASE and the click is never applied twice, which is
 *   what made "Start tracking" collapse the Runtime panel before.
 *
 * FPS COUNTER (stage 104)
 * ---------------------------------------------------------------------
 *   "Background" row in panel 03: preset chips Off / Solid / Blur, colour
 *   picker (RGBA) and opacity slider. Saved as [OVERLAY] FPSBackground,
 *   [STYLE] ColFPSBackground, and drawn as a rounded plate behind the OSD
 *   text (runtime_gui.cpp).
 */
#if RESHADE_GUI
#include "runtime.hpp"
#include <Windows.h>
#include <cstdlib>
#include "chams.hpp"
#include <imgui.h>
#include <imgui_internal.h>
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
static const ImVec4 kInner2   = ImVec4(0x1E/255.f,0x27/255.f,0x2F/255.f,1.f);
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
static float Clamp(float v,float lo,float hi){ return v<lo?lo:(v>hi?hi:v); }

// ---------- text helpers that can never overflow a rect --------------
static float TextW(const char* t){ return t?ImGui::CalcTextSize(t).x:0.f; }
// draws text clipped (with an ellipsis) into [x, x+maxw]
static void ClipText(ImDrawList* dl,ImVec2 p,float maxw,ImVec4 col,const char* t){
  if(!t||!t[0]||maxw<=0.f) return;
  float full=ImGui::CalcTextSize(t).x;
  if(full<=maxw){ dl->AddText(p,Col(col),t); return; }
  const char* end=t; float w=0.f;
  while(*end){
    float cw=ImGui::CalcTextSize(end,end+1).x;
    if(w+cw>maxw-ImGui::CalcTextSize("…").x) break;
    w+=cw; ++end;
  }
  dl->AddText(p,Col(col),t,end);
  dl->AddText(ImVec2(p.x+w,p.y),Col(col),"…");
}
// draws text centred inside a rect, clipped if needed
static void CenterText(ImDrawList* dl,ImVec2 a,ImVec2 b,ImVec4 col,const char* t){
  if(!t||!t[0]) return;
  float tw=ImGui::CalcTextSize(t).x;
  float x=a.x+(b.x-a.x-tw)*0.5f; if(x<a.x+2.f) x=a.x+2.f;
  float y=a.y+((b.y-a.y)-ImGui::GetFontSize())*0.5f;
  ClipText(dl,ImVec2(x,y),b.x-x,col,t);
}
// word-wrapped paragraph; returns the height used
static float WrapText(ImVec4 col,const char* t,float maxw){
  if(!t||!t[0]) return 0.f;
  ImFont* f=ImGui::GetFont(); float fs=ImGui::GetFontSize();
  float lineH=ImGui::GetTextLineHeight(); float space=ImGui::CalcTextSize(" ").x;
  const char* p=t; float y=0.f;
  ImDrawList* dl=ImGui::GetWindowDrawList();
  ImVec2 base=ImGui::GetCursorScreenPos();
  while(*p){
    // find how many characters fit
    const char* e=p; float w=0.f; const char* lastSpace=nullptr;
    while(*e){
      if(*e==' ') lastSpace=e;
      float cw=f->CalcTextSizeA(fs,FLT_MAX,0.f,e,e+1).x;
      if(w+cw>maxw){ if(lastSpace&&lastSpace>=p) { e=lastSpace; } break; }
      w+=cw; ++e;
      if(*e=='\n'){ lastSpace=nullptr; break; }
    }
    if(e==p) break; // nothing fits
    dl->AddText(f,fs,ImVec2(base.x,base.y+y),Col(col),p,e);
    y+=lineH;
    p=e; while(*p==' ') ++p;
  }
  ImGui::Dummy(ImVec2(maxw,y));
  return y;
}

// ---------- widgets ----------
// pure renderer: draws the 32x18 switch at 'p' from the value alone. It has no
// hit test, so a caller that already handled the click can never double-apply
// it, and a caller that only wants a mirror of a state can never steal a click.
void ToggleDraw(ImDrawList* dl,ImVec2 p,bool v,bool hovered){
  float k=Unit();
  float w=32*k,h=18*k;
  ImVec2 a=p; ImVec2 b(a.x+w,a.y+h); float r=h*0.5f;
  ImVec4 bg=v?kMint:ImVec4(0.10f,0.13f,0.16f,1.f);
  if(hovered) bg=Mix(bg,kText,0.10f);
  dl->AddRectFilled(a,b,Col(bg),r);
  if(!v) dl->AddRect(a,b,Col(kLine),r,0,Hair());
  float kr=6.f*k; float kx=v?(b.x-kr-2.f*k):(a.x+kr+2.f*k); float ky=a.y+h*0.5f;
  dl->AddCircleFilled(ImVec2(kx,ky),kr,Col(ImVec4(0.08f,0.10f,0.12f,1.f)),16);
  if(!v) dl->AddCircleFilled(ImVec2(kx,ky),kr-0.5f,Col(kMuted),16);
}
// row switch: hit test + render, unchanged behaviour for every row caller
bool AlexaToggle(const char* id,bool* v){
  float k=Unit();
  ImVec2 p0=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id,ImVec2(32*k,18*k));
  bool hovered=ImGui::IsItemHovered(); bool clicked=ImGui::IsItemClicked();
  if(clicked) *v=!*v;
  ToggleDraw(ImGui::GetWindowDrawList(),p0,*v,hovered);
  return clicked;
}
void Chevron(ImDrawList* dl,ImVec2 c,bool open,ImVec4 col4){
  float k=Unit(); float s=5.f*k; ImU32 col=Col(col4);
  if(open){ dl->AddLine(ImVec2(c.x-s*0.45f,c.y-s*0.2f),ImVec2(c.x,c.y+s*0.3f),col,1.6f*k); dl->AddLine(ImVec2(c.x+s*0.45f,c.y-s*0.2f),ImVec2(c.x,c.y+s*0.3f),col,1.6f*k); }
  else{ dl->AddLine(ImVec2(c.x-s*0.2f,c.y-s*0.45f),ImVec2(c.x+s*0.3f,c.y),col,1.6f*k); dl->AddLine(ImVec2(c.x-s*0.2f,c.y+s*0.45f),ImVec2(c.x+s*0.3f,c.y),col,1.6f*k); }
}

// ================= AUTO-FLOW ROW SYSTEM =============================
// A row is built in two passes: first we know how wide the right hand
// control block is, then either it is drawn on the same line as the label
// or it is moved under it. Nothing can overlap.
struct RowState { float y0; float labelH; bool stacked; };
static float s_ctrlW = 0.f;

static void RowBegin(){
  ImGui::PushID(&s_ctrlW); // unique-ish, keeps widget ids distinct per row
  s_ctrlW = 0.f;
  // nothing drawn yet: reserve nothing, real work in RowEnd
}
// Measures what the control block still needs, by drawing it into a scratch
// pass is not possible in ImGui, so callers state the control width via
// CtrlWidth() before RowEnd.
static void CtrlWidth(float w){ s_ctrlW = w; }

// two-line label; the hint is clipped to labelMaxW
static void Label2(const char* title,const char* hint,float labelMaxW=FLT_MAX){
  float k=Unit();
  ImDrawList* dl=ImGui::GetWindowDrawList();
  ImVec2 c=ImGui::GetCursorScreenPos();
  float tw=TextW(title);
  ClipText(dl,c,labelMaxW,kText,title);
  float h=ImGui::GetTextLineHeight();
  if(hint&&hint[0]){
    ImVec2 hc(c.x+0.f,c.y+h+2*k);
    float used=WrapText(kDim,hint,labelMaxW);
    h+=used+2*k;
  }
  (void)tw;
  ImGui::Dummy(ImVec2(0,h));
}
static void Label1(const char* title){
  ImGui::TextUnformatted(title);
}

// right aligned 32x18 toggle inside the current row, never over the label
static bool ToggleRight(const char* id,bool* v){
  float k=Unit();
  float tw=32*k, avail=ImGui::GetContentRegionAvail().x;
  float pad=ImGui::GetStyle().WindowPadding.x;
  (void)pad;
  float labelW = std::max(0.f, avail - tw - 10*k);
  ImGui::SameLine(std::max(labelW + 6*k, ImGui::GetCursorPosX()));
  ImGui::SetCursorPosY(ImGui::GetCursorPosY()+ (ImGui::GetTextLineHeight()-18*k)*0.5f);
  bool r=AlexaToggle(id,v);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (ImGui::GetTextLineHeight()-18*k)*0.5f);
  return r;
}

// value pill on the right of the row (read only)
static void ValuePill(const char* text,float w=92.f){
  float k=Unit();
  ImVec2 cur=ImGui::GetCursorScreenPos();
  float h=26*k;
  ImDrawList* dl=ImGui::GetWindowDrawList();
  dl->AddRectFilled(cur,ImVec2(cur.x+w,cur.y+h),Col(kInner),5*k);
  dl->AddRect(cur,ImVec2(cur.x+w,cur.y+h),Col(kLine),5*k,0,Hair());
  CenterText(dl,cur,ImVec2(cur.x+w,cur.y+h),kText,text);
  ImGui::Dummy(ImVec2(w,h));
}

// checkbox + hint, hint wraps under the box when the row is narrow
static bool CheckRow(const char* label,bool* v,const char* hint){
  float k=Unit();
  bool ch=false;
  ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(0,0,0,0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4(0,0,0,0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive,ImVec4(0,0,0,0));
  ImGui::PushStyleColor(ImGuiCol_CheckMark,kMint);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,3*k);
  if(ImGui::Checkbox(label,v)) ch=true;
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(4);
  if(hint&&hint[0]){
    float hx=ImGui::GetCursorPosX();
    float avail=ImGui::GetContentRegionAvail().x;
    float hw=TextW(hint);
    if(hw < avail-4*k){ ImGui::SameLine(0,12*k); ImGui::TextDisabled("%s",hint); }
    else { ImGui::SetCursorPosX(hx); WrapText(kMuted,hint,avail); }
  }
  return ch;
}

// label + right aligned value pill + full-width slider, all measured
static bool SliderRowF(const char* label,const char* id,float* v,float lo,float hi,const char* fmt,const char* /*unused*/){
  float k=Unit();
  float pillW=76*k;
  float avail=ImGui::GetContentRegionAvail().x;
  char val[32]; snprintf(val,sizeof(val),fmt,*v);
  ImGui::TextUnformatted(label);
  if(TextW(label)+pillW+12*k <= avail){
    ImGui::SameLine(avail-pillW,0);
  } else {
    // stacked: pill goes on its own line, right aligned
    ImGui::SameLine(); ImGui::Dummy(ImVec2(std::max(0.f,avail-pillW),0));
  }
  // pill (drawn, not a widget, so it cannot steal the slider's click)
  {
    ImVec2 cur=ImGui::GetCursorScreenPos(); float h=26*k;
    ImDrawList* dl=ImGui::GetWindowDrawList();
    dl->AddRectFilled(cur,ImVec2(cur.x+pillW,cur.y+h),Col(kInner),5*k);
    dl->AddRect(cur,ImVec2(cur.x+pillW,cur.y+h),Col(kLine),5*k,0,Hair());
    CenterText(dl,cur,ImVec2(cur.x+pillW,cur.y+h),kText,val);
    ImGui::Dummy(ImVec2(pillW,h));
  }
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,5*k);
  ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,5*k);
  ImGui::PushStyleColor(ImGuiCol_FrameBg,kInner);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,kInner2);
  ImGui::PushStyleColor(ImGuiCol_SliderGrab,kMint);
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,kMint);
  ImGui::PushStyleColor(ImGuiCol_Border,kLine);
  bool r=ImGui::SliderFloat(id,v,lo,hi,fmt,ImGuiSliderFlags_AlwaysClamp);
  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar(2);
  // min / max captions, measured so they never collide
  { float w=ImGui::GetContentRegionAvail().x;
    char mn[24],mx[24]; snprintf(mn,sizeof(mn),fmt,lo); snprintf(mx,sizeof(mx),fmt,hi);
    ImGui::TextDisabled("%s",mn);
    float mw=TextW(mx);
    if(w>TextW(mn)+mw+12*k){ ImGui::SameLine(w-mw,0); }
    else { ImGui::SameLine(); }
    ImGui::TextDisabled("%s",mx); }
  return r;
}
static bool SliderRowI(const char* label,const char* id,int* v,int lo,int hi,const char* suffix){
  float k=Unit();
  float pillW=76*k;
  float avail=ImGui::GetContentRegionAvail().x;
  char val[32]; snprintf(val,sizeof(val),"%d %s",*v,suffix?suffix:"");
  ImGui::TextUnformatted(label);
  if(TextW(label)+pillW+12*k <= avail) ImGui::SameLine(avail-pillW,0);
  else { ImGui::SameLine(); ImGui::Dummy(ImVec2(std::max(0.f,avail-pillW),0)); }
  {
    ImVec2 cur=ImGui::GetCursorScreenPos(); float h=26*k;
    ImDrawList* dl=ImGui::GetWindowDrawList();
    dl->AddRectFilled(cur,ImVec2(cur.x+pillW,cur.y+h),Col(kInner),5*k);
    dl->AddRect(cur,ImVec2(cur.x+pillW,cur.y+h),Col(kLine),5*k,0,Hair());
    CenterText(dl,cur,ImVec2(cur.x+pillW,cur.y+h),kText,val);
    ImGui::Dummy(ImVec2(pillW,h));
  }
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,5*k);
  ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,5*k);
  ImGui::PushStyleColor(ImGuiCol_FrameBg,kInner);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,kInner2);
  ImGui::PushStyleColor(ImGuiCol_SliderGrab,kMint);
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,kMint);
  ImGui::PushStyleColor(ImGuiCol_Border,kLine);
  bool r=ImGui::SliderInt(id,v,lo,hi,"%d",ImGuiSliderFlags_AlwaysClamp);
  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar(2);
  { float w=ImGui::GetContentRegionAvail().x;
    char mn[16],mx[16]; snprintf(mn,sizeof(mn),"%d",lo); snprintf(mx,sizeof(mx),"%d",hi);
    ImGui::TextDisabled("%s",mn);
    float mw=TextW(mx);
    if(w>TextW(mn)+mw+12*k){ ImGui::SameLine(w-mw,0); } else { ImGui::SameLine(); }
    ImGui::TextDisabled("%s",mx); }
  return r;
}

// label + dropdown: side by side when it fits, otherwise stacked
static bool ComboRow(const char* label,const char* id,int* sel,const char* items,int wantW=220){
  float k=Unit();
  float avail=ImGui::GetContentRegionAvail().x;
  float cw=(float)wantW*k;
  ImGui::TextUnformatted(label);
  if(cw < 120*k) cw=avail;
  if(TextW(label)+cw+12*k <= avail){
    ImGui::SameLine(avail - cw,0);
    ImGui::SetNextItemWidth(cw);
  } else {
    ImGui::SetNextItemWidth(avail);
  }
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,5*k);
  ImGui::PushStyleColor(ImGuiCol_FrameBg,kInner);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,kInner2);
  bool r=ImGui::Combo(id,sel,items);
  ImGui::PopStyleColor(2); ImGui::PopStyleVar();
  return r;
}

// label + colour field (swatch + hex + alpha). Returns true on edit.
static bool ColorRow(const char* label,const char* id,float* col4,bool alpha,bool compactSwatch=false){
  float k=Unit();
  float avail=ImGui::GetContentRegionAvail().x;
  float fw=218*k; if(compactSwatch) fw=170*k;
  ImGui::TextUnformatted(label);
  if(TextW(label)+fw+12*k <= avail) ImGui::SameLine(avail - fw,0);
  ImGui::PushID(id);
  ImVec2 cur=ImGui::GetCursorScreenPos();
  float h=28*k; float w = (ImGui::GetCursorPosX()+fw<=avail)?fw:avail;
  ImDrawList* dl=ImGui::GetWindowDrawList();
  dl->AddRectFilled(cur,ImVec2(cur.x+w,cur.y+h),Col(kInner),5*k);
  dl->AddRect(cur,ImVec2(cur.x+w,cur.y+h),Col(kLine),5*k,0,Hair());
  float sw=20*k;
  ImVec2 s0(cur.x+5*k,cur.y+(h-sw)*0.5f);
  ImU32 c=IM_COL32((int)(col4[0]*255),(int)(col4[1]*255),(int)(col4[2]*255),alpha?(int)(col4[3]*255):255);
  dl->AddRectFilled(s0,ImVec2(s0.x+sw,s0.y+sw),c,4*k);
  dl->AddRect(s0,ImVec2(s0.x+sw,s0.y+sw),Col(kLine),4*k,0,Hair());
  char hx[48];
  if(alpha) snprintf(hx,sizeof(hx),"#%02X%02X%02X  %d%%",(int)(col4[0]*255),(int)(col4[1]*255),(int)(col4[2]*255),(int)(col4[3]*100));
  else      snprintf(hx,sizeof(hx),"#%02X%02X%02X",(int)(col4[0]*255),(int)(col4[1]*255),(int)(col4[2]*255));
  float tx=s0.x+sw+8*k;
  ClipText(dl,ImVec2(tx,cur.y+(h-ImGui::GetFontSize())*0.5f),(cur.x+w-8*k)-tx-10*k,kText,hx);
  // chevron on the right edge
  float cxp=cur.x+w-9*k; float cyp=cur.y+h*0.5f;
  dl->AddLine(ImVec2(cxp-3*k,cyp-2*k),ImVec2(cxp,cyp+2*k),Col(kDim),1.4f*k);
  dl->AddLine(ImVec2(cxp+3*k,cyp-2*k),ImVec2(cxp,cyp+2*k),Col(kDim),1.4f*k);
  ImGui::InvisibleButton("##sw",ImVec2(w,h));
  bool hovered=ImGui::IsItemHovered();
  if(hovered) dl->AddRect(cur,ImVec2(cur.x+w,cur.y+h),Col(kMint),5*k,0,Hair());
  bool r=false;
  if(ImGui::IsItemDeactivated() && hovered) ImGui::OpenPopup("pick");
  if(ImGui::BeginPopup("pick")){
    ImGui::PushStyleColor(ImGuiCol_PopupBg,kCard);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,kInner);
    if(alpha){ if(ImGui::ColorPicker4("##cp",col4,ImGuiColorEditFlags_AlphaBar|ImGuiColorEditFlags_NoSidePreview)) r=true; }
    else     { if(ImGui::ColorPicker3("##cp3",col4)) r=true; }
    ImGui::PopStyleColor(2);
    ImGui::EndPopup();
  }
  ImGui::PopID();
  return r;
}

// segmented chips (Fill mode, FPS background presets)
static bool Chips(const char* id,const char* const* labels,int count,int* sel,float fullW,bool icons=false){
  float k=Unit();
  float gap=6*k; float w=(fullW-gap*(count-1))/count; float h=32*k;
  bool changed=false;
  ImGui::PushID(id);
  for(int i=0;i<count;++i){
    if(i) ImGui::SameLine(0,gap);
    bool selected=(*sel==i);
    ImVec2 cur=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton((std::string("##c")+std::to_string(i)).c_str(),ImVec2(w,h));
    bool hov=ImGui::IsItemHovered();
    ImVec2 b(cur.x+w,cur.y+h);
    ImDrawList* dl=ImGui::GetWindowDrawList();
    ImVec4 bg=selected?kMintDark:kInner; ImVec4 bd=selected?kMint:kLine;
    if(hov&&!selected) bg=Mix(kInner,kText,0.05f);
    dl->AddRectFilled(cur,b,Col(bg),6*k);
    dl->AddRect(cur,b,Col(bd),6*k,0,Hair());
    // dot marker
    float mr=4*k; ImVec2 m(cur.x+11*k,cur.y+h*0.5f);
    dl->AddCircle(m,mr*1.6f,Col(selected?kMint:kLine),16,0.f);
    if(selected) dl->AddCircleFilled(m,mr,Col(kMint),16);
    ClipText(dl,ImVec2(m.x+mr*2.f+4*k,cur.y+(h-ImGui::GetFontSize())*0.5f),(cur.x+w-6*k)-(m.x+mr*2.f+4*k),selected?kText:kDim,labels[i]);
    if(ImGui::IsItemClicked() && !selected){ *sel=i; changed=true; }
  }
  ImGui::PopID();
  return changed;
}

// info strip (mint title + dim body that wraps)
static void InfoStrip(const char* title,const char* body){
  float k=Unit();
  ImVec2 cur=ImGui::GetCursorScreenPos();
  float w=ImGui::GetContentRegionAvail().x;
  float pad=12*k;
  float inner=w-pad*2.f;
  float th=ImGui::GetTextLineHeight();
  // measure body height
  float bodyH=0.f;
  { // dry run by counting wraps
    ImFont* f=ImGui::GetFont(); float fs=ImGui::GetFontSize();
    float lineH=ImGui::GetTextLineHeight();
    const char* p=body; bodyH=lineH;
    const char* e=p; float acc=0.f; const char* ls=nullptr;
    float lines=1.f;
    while(*e){ if(*e==' ') ls=e; float cw=f->CalcTextSizeA(fs,FLT_MAX,0.f,e,e+1).x; if(acc+cw>inner){ acc=0.f; lines+=1.f; if(ls==e) { /*ok*/ } e=ls?ls+1:e+1; if(*e) { e=e; } } acc+=cw; ++e; }
    bodyH=lines*lineH;
    (void)p;
  }
  float h=pad+th+3*k+bodyH+pad;
  ImDrawList* dl=ImGui::GetWindowDrawList();
  dl->AddRectFilled(cur,ImVec2(cur.x+w,cur.y+h),Col(kInner),7*k);
  dl->AddRect(cur,ImVec2(cur.x+w,cur.y+h),Col(kLine),7*k,0,Hair());
  dl->AddText(ImVec2(cur.x+pad,cur.y+pad),Col(kMint),title);
  ImGui::SetCursorScreenPos(ImVec2(cur.x+pad,cur.y+pad+th+3*k));
  WrapText(kDim,body,inner);
  ImGui::SetCursorScreenPos(ImVec2(cur.x,cur.y+h+6*k));
}

// ---------- section card ----------
bool BeginFigmaSection(const char* id,const char* num,const char* title,const char* subtitle,bool* tog){
  float k=Unit(); bool &open=g_open[id]; bool eff=open;
  ImVec2 cur=ImGui::GetCursorScreenPos(); float w=ImGui::GetContentRegionAvail().x;
  float headerH=ImGui::GetFrameHeight()+10.f*k; if(subtitle&&subtitle[0]) headerH+=14.f*k;
  float r=Rounding();
  ImGui::PushID(id);
  // ---------------------------------------------------------------
  // Header hit boxes (stage 108 -- "switch is blurred and cannot be turned on")
  // ---------------------------------------------------------------
  // The header used to be ONE InvisibleButton covering the full width, with the
  // toggle drawn on top of it afterwards. So the switch could be seen but never
  // clicked: the header item took the press, and clicking there collapsed the
  // panel instead. Its look was only "hovered" while the mouse was over the
  // header, which read as a blurred / disabled switch.
  //
  // Now the header is split into two hit boxes that never overlap:
  //   - label zone  : everything left of the toggle  -> collapse / expand
  //   - toggle zone : the switch itself              -> the real on/off
  // The toggle is registered FIRST, so nothing can ever be drawn over it.
  float togw=32*k; float togpad=10*k;
  float togx=cur.x+w-togpad-togw; float togy=cur.y+(headerH-18*k)*0.5f;
  bool togPressed=false, togHovered=false;
  if(tog){
    ImGui::SetCursorScreenPos(ImVec2(togx,togy));
    ImGui::PushID("tog");
    // 6px halo around the switch, so a slightly-off click still lands
    ImGui::SetCursorScreenPos(ImVec2(togx-6*k,togy-6*k));
    togPressed=ImGui::InvisibleButton("##toghit",ImVec2(togw+12*k,18*k+12*k));
    togHovered=ImGui::IsItemHovered();
    if(togPressed){ *tog=!*tog; reshade::chams::save(); }
    ImGui::PopID();
  }
  float labelW=std::max(1.f, (tog?togx-6*k:cur.x+w)-cur.x);
  ImGui::SetCursorScreenPos(cur);
  bool headerPressed=ImGui::InvisibleButton("##hdr",ImVec2(labelW,headerH));
  bool hovered=ImGui::IsItemHovered(); bool held=ImGui::IsItemActive();
  if(headerPressed){ open=!open; eff=open; }
  ImDrawList* dl=ImGui::GetWindowDrawList();
  ImVec4 hdrCol=held?Mix(kCard,kText,0.04f):hovered?Mix(kCard,kText,0.03f):kCard;
  ImDrawFlags hc=eff?ImDrawFlags_RoundCornersTop:ImDrawFlags_RoundCornersAll;
  dl->AddRectFilled(cur,ImVec2(cur.x+w,cur.y+headerH),Col(hdrCol),r,hc);
  dl->AddRect(cur,ImVec2(cur.x+w,cur.y+headerH),Col(kLine),r,hc,Hair());
  float pad=12.f*k; float ty=cur.y+9.f*k;
  Chevron(dl,ImVec2(cur.x+pad+2,cur.y+headerH*0.5f - ((subtitle&&subtitle[0])?5*k:0)),eff, eff?kMint:kDim);
  float tx=cur.x+pad+16.f*k;
  float tMax=cur.x+w-pad- (num? 34.f*k:0.f) - (tog? togw+8*k:0.f) - tx;
  ClipText(dl,ImVec2(tx,ty),tMax,kText,title);
  if(subtitle&&subtitle[0]){
    float sy=ty+ImGui::GetFontSize()+3*k;
    ClipText(dl,ImVec2(tx,sy),tMax,kDim,subtitle);
  }
  if(num){ ImVec2 ns=ImGui::CalcTextSize(num); dl->AddText(ImVec2(cur.x+w-10*k-ns.x- (tog? togw+6*k:0.f),ty),Col(kDim),num); }
  if(tog){
    // drawn from state only: the click was already handled above
    ToggleDraw(dl,ImVec2(togx,togy),*tog,togHovered);
  }
  ImGui::PopID();
  ImGui::SetCursorScreenPos(ImVec2(cur.x,cur.y+headerH));
  if(!eff){ ImGui::Dummy(ImVec2(0,7*k)); return false; }
  ImGui::PushStyleColor(ImGuiCol_ChildBg,kCard);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,r);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(12*k,12*k));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,Hair());
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(8*k,7*k));
  ImGui::BeginChild((std::string(id)+"_body").c_str(),ImVec2(w,0),ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY);
  return true;
}
void EndFigmaSection(){ ImGui::EndChild(); ImGui::PopStyleVar(4); ImGui::PopStyleColor(1); ImGui::Dummy(ImVec2(0,7*Unit())); }
void Divider(){ ImGui::Dummy(ImVec2(0,1)); ImDrawList* dl=ImGui::GetWindowDrawList(); ImVec2 c=ImGui::GetCursorScreenPos(); float w=ImGui::GetContentRegionAvail().x; dl->AddLine(ImVec2(c.x,c.y),ImVec2(c.x+w,c.y),Col(kLine),Hair()); ImGui::Dummy(ImVec2(0,5*Unit())); }
void SubLabel(const char* t){ float k=Unit(); ImGui::Dummy(ImVec2(0,3*k)); ImGui::TextColored(kDim,"%s",t); }

void HeaderChamsByAlexa(){
  float k=Unit(); float w=ImGui::GetContentRegionAvail().x; float h=70*k;
  ImVec2 cur=ImGui::GetCursorScreenPos(); ImDrawList* dl=ImGui::GetWindowDrawList();
  dl->AddRectFilled(cur,ImVec2(cur.x+w,cur.y+h),Col(kCard),Rounding());
  dl->AddRect(cur,ImVec2(cur.x+w,cur.y+h),Col(kLine),Rounding(),0,Hair());
  const char* a="Chams menu"; const char* b="by"; const char* c="AlexaInc";
  float tot=TextW(a)+TextW(b)+TextW(c)+12*k;
  float x=cur.x+(w-tot)*0.5f; float y=cur.y+h*0.5f-ImGui::GetFontSize()*0.5f;
  dl->AddText(ImVec2(x,y),Col(kText),a); x+=TextW(a)+6*k;
  dl->AddText(ImVec2(x,y),Col(kDim),b);  x+=TextW(b)+6*k;
  dl->AddText(ImVec2(x,y),Col(kMint),c);
  ImGui::SetCursorScreenPos(ImVec2(cur.x,cur.y+h+6*k));
}
void FooterHazu(){
  float k=Unit(); float w=ImGui::GetContentRegionAvail().x; float h=32*k;
  ImVec2 cur=ImGui::GetCursorScreenPos(); ImDrawList* dl=ImGui::GetWindowDrawList();
  dl->AddRectFilled(cur,ImVec2(cur.x+w,cur.y+h),Col(kCard),Rounding());
  dl->AddRect(cur,ImVec2(cur.x+w,cur.y+h),Col(kLine),Rounding(),0,Hair());
  const char* a="hazu"; const char* b=" @ "; const char* d="alexainc";
  float tw=TextW(a)+TextW(b)+TextW(d)+10*k;
  float cx=cur.x+(w-tw)*0.5f; float cy=cur.y+h*0.5f-ImGui::GetFontSize()*0.5f;
  dl->AddCircleFilled(ImVec2(cx-9*k,cy+ImGui::GetFontSize()*0.5f),3*k,Col(kMint),8);
  dl->AddText(ImVec2(cx,cy),Col(kMint),a);
  dl->AddText(ImVec2(cx+TextW(a),cy),Col(kDim),b);
  dl->AddText(ImVec2(cx+TextW(a)+TextW(b),cy),Col(kText),d);
  ImGui::SetCursorScreenPos(ImVec2(cur.x,cur.y+h+6*k));
}
} // anon

void reshade::runtime::draw_gui_chams(){
  static bool loaded=false; if(!loaded){ chams::load(); loaded=true; }
  chams::settings &s=chams::g_settings;
  ImGui::PushStyleColor(ImGuiCol_WindowBg,kPage);
  ImGui::PushStyleColor(ImGuiCol_Text,kText);
  ImGui::PushStyleColor(ImGuiCol_Border,kLine);
  ImGui::PushStyleColor(ImGuiCol_CheckMark,kMint);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(8*Unit(),8*Unit()));
  HeaderChamsByAlexa();

  // ================= MASTER SWITCH (auto arranged) =================
  {
    float k=Unit(); float w=ImGui::GetContentRegionAvail().x;
    ImVec2 cur=ImGui::GetCursorScreenPos(); float r=Rounding();
    float pad=14*k;
    float th=ImGui::GetTextLineHeight();
    float h=pad+th+4*k+th+pad;
    ImDrawList* dl=ImGui::GetWindowDrawList();
    dl->AddRectFilled(cur,ImVec2(cur.x+w,cur.y+h),Col(kCard),r);
    dl->AddRect(cur,ImVec2(cur.x+w,cur.y+h),Col(kLine),r,0,Hair());
    // left column, width depends on what the right column needs
    const char* prof="Default profile";
    float rightW=std::max(TextW(prof),TextW("chams.ini  /  CHAMS"))+4*k;
    float leftW=w-pad*2-rightW-16*k;
    // row 1: label + toggle + F8 + hint
    ImGui::SetCursorScreenPos(ImVec2(cur.x+pad,cur.y+pad));
    ImGui::TextUnformatted("Master switch");
    ImGui::SameLine(0,8*k); AlexaToggle("##master",&s.enabled);
    ImGui::SameLine(0,8*k);
    { char key[16]; snprintf(key,sizeof(key),"F%d",(chams::k_toggle_key==0x77)?8:8);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(6*k,2*k));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,4*k);
      ImGui::PushStyleColor(ImGuiCol_Button,kInner);
      ImGui::Button(key);
      ImGui::PopStyleColor(); ImGui::PopStyleVar(2); }
    if(TextW("Toggle in game") + ImGui::GetCursorPosX() + 8*k < leftW + pad){
      ImGui::SameLine(0,8*k); ImGui::TextDisabled("Toggle in game");
    }
    // row 2: state
    ImGui::SetCursorScreenPos(ImVec2(cur.x+pad,cur.y+pad+th+4*k));
    ImGui::TextColored(s.enabled?kMint:kMuted, s.enabled?"Chams enabled":"Chams disabled");
    if(TextW("Per-draw mask, depth claimed") + ImGui::GetCursorPosX() + 10*k < leftW + pad)
    { ImGui::SameLine(0,10*k); ImGui::TextDisabled("Per-draw mask, depth claimed"); }
    // right column
    ImGui::SetCursorScreenPos(ImVec2(cur.x+w-pad-rightW,cur.y+pad));
    ImGui::TextUnformatted(prof);
    ImGui::SetCursorScreenPos(ImVec2(cur.x+w-pad-TextW("chams.ini  /  CHAMS"),cur.y+pad+th+4*k));
    ImGui::TextDisabled("chams.ini  /  CHAMS");
    ImGui::SetCursorScreenPos(ImVec2(cur.x,cur.y+h+7*k));
  }

  // ================= RUNTIME CAPTURE =================
  // Click handling (stage 106):
  //   The collapse hit box is an InvisibleButton over the whole header, so any
  //   button placed on top of it lost the click to the header (that is why
  //   "Start tracking" never reacted). The header is now split into TWO hit
  //   areas that never overlap:
  //     - a narrow chevron zone on the left (this toggles the panel)
  //     - the rest of the header (title + badge), which does nothing
  //   The action buttons are drawn to the right of the title, outside both
  //   zones, so their clicks always reach them. Their label is static
  //   ("Start tracking" / "Add marker"); the tracking state is shown in the
  //   body, so the button can never appear to change into a collapse control.
  {
    static bool s_tracking = false;
    bool &open=g_open["runtime"];
    float k=Unit(); float w=ImGui::GetContentRegionAvail().x;
    ImVec2 cur=ImGui::GetCursorScreenPos(); float r=Rounding();
    float pad=12*k; float th=ImGui::GetTextLineHeight();
    float btnH=26*k;
    float bwStart=TextW("Stop tracking")+30*k, bwMarker=TextW("Add marker")+30*k;
    float badgeW=TextW("EXPERIMENTAL")+18*k;
    float hh=std::max(pad*2+th, btnH+pad*2);
    float chevW=34*k;                 // collapse zone (left)
    float titleX=cur.x+chevW;
    float badgeX=titleX+TextW("Runtime capture")+12*k;
    float btnX=cur.x+w-pad-bwMarker-8*k-bwStart;
    bool badgeFits = badgeX+badgeW+10*k < btnX;

    ImGui::PushID("runtime");
    // 1) chevron / collapse zone only
    ImGui::SetCursorScreenPos(cur);
    bool chevPressed=ImGui::InvisibleButton("##collapse",ImVec2(chevW,hh));
    bool chevHov=ImGui::IsItemHovered();
    // 2) title zone: decorative, no click at all (nothing to steal)
    ImDrawList* dl=ImGui::GetWindowDrawList();
    ImVec4 hc=chevHov?Mix(kCard,kText,0.03f):kCard;
    dl->AddRectFilled(cur,ImVec2(cur.x+w,cur.y+hh),Col(hc),r,ImDrawFlags_RoundCornersTop);
    dl->AddRect(cur,ImVec2(cur.x+w,cur.y+hh),Col(kLine),r,ImDrawFlags_RoundCornersTop,Hair());
    Chevron(dl,ImVec2(cur.x+16*k,cur.y+hh*0.5f),open,open?kMint:kDim);
    ClipText(dl,ImVec2(titleX,cur.y+(hh-th)*0.5f),badgeX-titleX-6*k,kText,"Runtime capture");
    if(badgeFits){
      ImVec2 ba(badgeX,cur.y+(hh-18*k)*0.5f), bb(badgeX+badgeW,ba.y+18*k);
      dl->AddRectFilled(ba,bb,Col(kInner),4*k);
      CenterText(dl,ba,bb,kDim,"EXPERIMENTAL");
    }
    if(chevPressed) open=!open;

    // 3) the two action buttons, right of the title, outside every hit zone
    ImGui::SetCursorScreenPos(ImVec2(btnX,cur.y+(hh-btnH)*0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,5*k);
    ImGui::PushStyleColor(ImGuiCol_Button, s_tracking?kMintDark:kInner2);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, s_tracking?Mix(kMintDark,kMint,0.25f):Mix(kInner2,kText,0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, s_tracking?kMintDark:kInner);
    ImGui::PushStyleColor(ImGuiCol_Text, s_tracking?kMint:kText);
    if(ImGui::Button(s_tracking?"Stop tracking":"Start tracking",ImVec2(bwStart,btnH))){
      // Stage 115: this used to flip a local bool and write one log line. The
      // recorder existed but nothing ever started it, so no session directory was
      // created and the counters could never move.
      if(s_tracking){
        chams::capture_session_stop();
        s_tracking=false;
        chams::dump_line("=== runtime capture stopped ===");
      } else {
        s_tracking=chams::capture_session_start();
        if(s_tracking) chams::dump_line("=== runtime capture started ===");
        open=true;
      }
    }
    ImGui::PopStyleColor(4);
    ImGui::SameLine(0,8*k);
    ImGui::PushStyleColor(ImGuiCol_Button,kInner2);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,Mix(kInner2,kText,0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,kInner);
    if(ImGui::Button("Add marker",ImVec2(bwMarker,btnH))){ chams::dump_line("=== marker ===  runtime capture marker"); }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    ImGui::PopID();

    ImGui::SetCursorScreenPos(ImVec2(cur.x,cur.y+hh));
    if(open){
      ImGui::PushStyleColor(ImGuiCol_ChildBg,kCard);
      ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,r);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(12*k,10*k));
      ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,Hair());
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(8*k,7*k));
      ImGui::BeginChild("runtime_body",ImVec2(w,0),ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY);
      // tracking state, shown here instead of on the button
      ImGui::TextColored(s_tracking?kMint:kMuted, s_tracking?"Tracking active":"Tracking stopped");
      { // live values; these were hardcoded string literals before stage 115
        unsigned fr=0,ct=0,dr=0,dp=0;
        chams::capture_counters(&fr,&ct,&dr,&dp);
        if(s_tracking && !chams::capture_is_active()) s_tracking=false;   // capture died: reflect it
        char st0[48],st1[48],st2[48],st3[48];
        snprintf(st0,sizeof(st0),"Frames %u",fr);
        snprintf(st1,sizeof(st1),"Contexts %u",ct);
        snprintf(st2,sizeof(st2),"Draw records %u",dr);
        snprintf(st3,sizeof(st3),"Dropped %u",dp);
        const char* stats[4]={st0,st1,st2,st3};
        float avail=ImGui::GetContentRegionAvail().x; float x=0.f; bool same=false;
        for(int i=0;i<4;++i){ float wds=TextW(stats[i]); if(x+wds+16*Unit()>avail){ x=0.f; same=false; }
          if(same){ ImGui::SameLine(0,18*Unit()); }
          ImGui::TextDisabled("%s",stats[i]); x+=wds+18*Unit(); same=true; } }
      Divider();
      // dump rows: checkbox + hint (wrapped) + action on its own line
      if(CheckRow("Dump mode",&s.dump,"Write draw info to ReShade.log · 2,000 lines / activation")) chams::save();
      // where the recorded session actually is
      { const char* dir=chams::capture_session_path();
        if(dir && dir[0]){
          ImGui::TextDisabled("Recording to:");
          ImGui::PushStyleColor(ImGuiCol_Text,kMuted);
          ImGui::TextWrapped("%s",dir);
          ImGui::PopStyleColor();
        } else {
          WrapText(kMuted,"Nothing recorded yet. Start tracking and draw a few frames.",ImGui::GetContentRegionAvail().x);
        } }
      { float avail=ImGui::GetContentRegionAvail().x; float bw=TextW("Copy dump path")+24*Unit();
        ImGui::SetCursorPosX(std::max(0.f,avail-bw));
        if(ImGui::Button("Copy dump path",ImVec2(bw,0))){
          ImGui::SetClipboardText(chams::dump_path());
        } }
      { float avail=ImGui::GetContentRegionAvail().x; float bw=TextW("Copy capture path")+24*Unit();
        if(avail > bw+8*Unit()){
          ImGui::SameLine();
          if(ImGui::Button("Copy capture path",ImVec2(bw,0))){
            const char* dir=chams::capture_session_path();
            ImGui::SetClipboardText((dir && dir[0]) ? dir : "(no capture session)");
          }
        } }
      if(CheckRow("Dump ALL draws",&s.dump_all,"Find missing pieces · log every draw, 3,000 records")) chams::save();
      ImGui::TextDisabled("Dump text: %TEMP%\\chams-dump.txt · capture tree: %TEMP%\\capture\\session-*");
      ImGui::EndChild(); ImGui::PopStyleVar(4); ImGui::PopStyleColor(1);
    }
    ImGui::SetCursorScreenPos(ImVec2(cur.x,ImGui::GetCursorScreenPos().y));
    ImGui::Dummy(ImVec2(0,7*Unit()));
  }

  // ================= CATEGORIES bar =================
  {
    float w=ImGui::GetContentRegionAvail().x;
    ImGui::TextColored(kMint,"CATEGORIES");
    int expanded=0; for(auto &kv:g_open) if(kv.first!="runtime"&&kv.first!="logs"&&kv.second) ++expanded;
    char lab[32]; snprintf(lab,sizeof(lab),"%d panels · expanded",expanded);
    float rw=TextW(lab);
    if(w>rw+40){ ImGui::SameLine(w-rw,0); ImGui::TextDisabled("%s",lab); }
    else { ImGui::SameLine(); ImGui::TextDisabled("%s",lab); }
    ImGui::Dummy(ImVec2(0,3*Unit()));
  }

  // ================= 01 General =================
  if(BeginFigmaSection("general","01","General","Choose how characters are detected and rendered.",nullptr)){
    SubLabel("Render mode");
    { int rm=(s.render_mode==1)?0:1;
      if(ComboRow("","##rendermode",&rm,"Direct (recommended)\0Overlay (experimental)\0",210)){
        s.render_mode=(rm==0)?1:0; chams::save(); } }
    WrapText(kMuted,"Per-draw passes, depth tested by the game. Overlay composites over the finished frame.",ImGui::GetContentRegionAvail().x);
    Divider();
    SubLabel("Character detection mode");
    if(ComboRow("","##detectmode",&s.detect_mode,"Strict\0Wide (default)\0Skinned\0All\0",210)) chams::save();
    WrapText(kMuted,"Wide checks 8 avatar-only uniforms for skins, cloth and hair. Strict is the original DLL rule.",ImGui::GetContentRegionAvail().x);
    Divider();
    Label1("See enemies through walls"); ToggleRight("##through",&s.through_walls);
    WrapText(kMuted,"Includes glue / ice walls. Off: only occluded parts are highlighted.",ImGui::GetContentRegionAvail().x);
    Label1("Border-compatible fill / wire (experimental)");
    if(ToggleRight("##bordercompat",&s.border_compat)) chams::save();
    WrapText(kMuted,"Direct + through walls only: draw fill / wire over all depth. May tint visible parts too; a later wall can still cover them.",ImGui::GetContentRegionAvail().x);
    Divider();
    Label1("Also paint hair / cloth shaders"); ToggleRight("##wide",&s.wide_match);
    WrapText(kMuted,"Wide match uses _BackLightDir instead of _CharaLightIntensity.",ImGui::GetContentRegionAvail().x);
    SubLabel("Renderer behaviour");
    WrapText(kMuted,"Direct repeats each character draw inside the game's own frame. Overlay renders a mask and composites it on top at the end of the frame.",ImGui::GetContentRegionAvail().x);
    InfoStrip("Overlay is experimental","A separate mask buffer paints on top of the finished frame. If you ever get a black screen, switch back to Direct or toggle F8 off and on.");
    EndFigmaSection();
  }

  // ================= 02 Third person =================
  if(BeginFigmaSection("third","02","Third person","Keep your own character clear without losing nearby enemies.",nullptr)){
    Label1("No chams on my own character"); ToggleRight("##hideown",&s.hide_own);
    WrapText(kMuted,"Automatically excludes the closest draw inside the safe distance.",ImGui::GetContentRegionAvail().x);
    Divider();
    SliderRowF("Safe cap for my character","##owncap",&s.own_distance,0.5f,15.f,"%.1f m","");
    Divider();
    InfoStrip("Protect close enemies","A draw farther than the safe cap is never treated as your own character, so a nearby enemy is still highlighted.");
    SubLabel("AUTO DETECTION");
    WrapText(kMuted,"Stable distance + nearest draw, re-evaluated every frame.",ImGui::GetContentRegionAvail().x);
    EndFigmaSection();
  }

  // ================= 03 FPS counter =================
  if(BeginFigmaSection("fps","03","FPS counter","A lightweight performance overlay, positioned your way.",nullptr)){
    bool fpsOn=(_show_fps!=0);
    Label1("Show FPS counter"); 
    if(ToggleRight("##fpsshow",&fpsOn)){ _show_fps=fpsOn?1:0; save_config(); }
    WrapText(kMuted,"Always on. Uses the existing overlay GL guard to prevent flicker.",ImGui::GetContentRegionAvail().x);
    Divider();
    SubLabel("Position");
    { int pos=(int)_fps_pos;
      if(ComboRow("","##fpspos",&pos,"Top left\0Top right\0Bottom left\0Bottom right\0",210)){ _fps_pos=pos; save_config(); } }
    Divider();
    SliderRowF("Scale","##fpsscale",&_fps_scale,0.2f,2.5f,"%.1fx","");
    Divider();
    SubLabel("Text colour · RGBA");
    ColorRow("","fpscol",_fps_col,true);
    Divider();
    // ---------- NEW: background colour for the FPS counter ----------
    SubLabel("Background");
    { const char* bgLabels[3]={"Off","Solid","Blur"};
      int bg=(int)std::min<unsigned>(_fps_bg,(unsigned)2);
      if(Chips("fpsbg",bgLabels,3,&bg,ImGui::GetContentRegionAvail().x)){
        _fps_bg=(unsigned)bg; save_config(); } }
    if(_fps_bg>0){
      WrapText(kMuted,"A rounded plate is drawn behind the counter using the colour below.",ImGui::GetContentRegionAvail().x);
      ColorRow("Plate colour · RGBA","fpsbgcol",_fps_bg_col,true);
      SliderRowF("Opacity","##fpsbgop",&_fps_bg_col[3],0.f,1.f,"%.2f","");
      SliderRowF("Corner radius","##fpsbgcorner",&_fps_bg_round,0.f,24.f,"%.0f px","");
      SubLabel("Plate padding");
      SliderRowF("Horizontal","##fpsbgph",&_fps_bg_pad[0],0.f,32.f,"%.0f px","");
      SliderRowF("Vertical","##fpsbgpv",&_fps_bg_pad[1],0.f,32.f,"%.0f px","");
    } else {
      WrapText(kMuted,"Off draws the text straight on the game image, like before.",ImGui::GetContentRegionAvail().x);
    }
    Divider();
    WrapText(kMuted,"Saved in chams.ini [OVERLAY] / [STYLE] — ShowFPS, FPSPosition, FPSScale, ColFPSBackground.",ImGui::GetContentRegionAvail().x);
    EndFigmaSection();
  }

  // ================= 04 Glow & fill =================
  if(BeginFigmaSection("glow","04","Glow & fill","Control body fill, clothing detail and visible / hidden colours.",&s.glow)){
    SubLabel("Fill mode");
    { const char* modes[4]={"Solid colour","Blend","Real clothes","Clothes + light"};
      int m=s.glow_mode;
      if(Chips("fillmode",modes,4,&m,ImGui::GetContentRegionAvail().x)){ s.glow_mode=m; chams::save(); } }
    if(!s.glow){
      InfoStrip("Player highlight is off",
                "The switch in this panel's header turns body fill on and off. Turn it on to apply the settings below.");
    }
    switch(s.glow_mode){
      case 1: WrapText(kMuted,"Blend keeps the real clothes and layers the colour over them.",ImGui::GetContentRegionAvail().x); break;
      case 2: WrapText(kMuted,"Real clothes drawn through walls — best for outfit visibility.",ImGui::GetContentRegionAvail().x); break;
      case 3: WrapText(kMuted,"Real clothes with a light tint over them.",ImGui::GetContentRegionAvail().x); break;
      default: WrapText(kMuted,"Whole body filled with one solid colour — classic chams.",ImGui::GetContentRegionAvail().x); break;
    }
    Divider();
    SliderRowF("Glow strength","##glowalpha",&s.glow_alpha,0.05f,1.f,"%.2f","");
    Divider();
    ColorRow("Through walls · colour","glowthrough",s.glow_through_color,false);
    ColorRow("Open part · colour","glowopen",s.glow_open_color,false);
    Label1("Open part enabled"); ToggleRight("##glowopen_t",&s.glow_open);
    EndFigmaSection();
  }

  // ================= 04b Minimap marks =================
  // ================= 04b Radar =================
  // The square is dragged and resized HERE, in the menu: this is the only place
  // with a mouse that we own, so with the menu closed nothing can move it and the
  // position is locked. Scale is 1:1: pixels / metres is the same on x and z.
  {
    ImGuiIO &io=ImGui::GetIO();
    const float side=std::max(80.f,s.radar_square_px);
    const float x=s.radar_square_x, y=s.radar_square_y;
    const ImVec2 m=io.MousePos;
    const float handle=14*Unit();
    const bool overSquare = m.x>=x-4 && m.x<=x+side+4 && m.y>=y-4 && m.y<=y+side+4;
    const bool overHandle = m.x>=x+side-handle && m.x<=x+side+8 && m.y>=y+side-handle && m.y<=y+side+8;
    static bool s_dragging=false; static bool s_resizing=false;
    static float s_grab_x=0.f, s_grab_y=0.f;
    if(overHandle && ImGui::IsMouseClicked(ImGuiMouseButton_Left)){ s_resizing=true; s_grab_x=m.x-x; s_grab_y=m.y-y; }
    else if(overSquare && ImGui::IsMouseClicked(ImGuiMouseButton_Left)){ s_dragging=true; s_grab_x=m.x-x; s_grab_y=m.y-y; }
    if(s_resizing && ImGui::IsMouseDown(ImGuiMouseButton_Left)){
      const float want=std::max(80.f,std::max(m.x-x,m.y-y));
      if(want!=s.radar_square_px){ s.radar_square_px=want; chams::save(); }
    } else s_resizing=false;
    if(s_dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)){
      s.radar_square_x=std::max(0.f,std::min(io.DisplaySize.x-side,m.x-s_grab_x));
      s.radar_square_y=std::max(0.f,std::min(io.DisplaySize.y-side,m.y-s_grab_y));
      chams::save();
    } else s_dragging=false;
    // faint outline of the square inside the menu, so it can be found by eye
    ImDrawList *dl=ImGui::GetForegroundDrawList();
    dl->AddRect(ImVec2(x,y),ImVec2(x+side,y+side),ImGui::GetColorU32(ImVec4(kMint.x,kMint.y,kMint.z,0.55f)),0.f,0,1.5f);
    dl->AddRectFilled(ImVec2(x+side-handle,y+side-handle),ImVec2(x+side,y+side),ImGui::GetColorU32(ImVec4(kMint.x,kMint.y,kMint.z,0.35f)),2.f);
    dl->AddCircleFilled(ImVec2(x+side*0.5f,y+side*0.5f),4.f,ImGui::GetColorU32(ImVec4(kMint.x,kMint.y,kMint.z,0.90f)),12);
    if(s_resizing) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
    else if(s_dragging||overSquare) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
  }

  if(BeginFigmaSection("radar","04b","Radar","A player-centred radar of your own, built from the captured matrices. Drag the square with the menu open, resize from its bottom right corner.",&s.radar_marks)){
    SubLabel("Position (drag in game)");
    WrapText(kMuted,"The square outline and the dot in its middle are drawn while the menu is open. Drag the square anywhere, pull its bottom right corner to resize. Close the menu and the position locks.",ImGui::GetContentRegionAvail().x);
    SliderRowF("Position X","##radarx",&s.radar_square_x,0.f,3840.f,"%.0f px","");
    SliderRowF("Position Y","##radary",&s.radar_square_y,0.f,2160.f,"%.0f px","");
    Divider();
    SubLabel("Size and area");
    SliderRowF("Square size","##radarsize",&s.radar_square_px,80.f,1200.f,"%.0f px","");
    WrapText(kMuted,"How big the radar is on screen. Same as pulling its bottom right corner.",ImGui::GetContentRegionAvail().x);
    SliderRowF("Area across","##radararea",&s.radar_area_m,10.f,600.f,"%.0f m","");
    WrapText(kMuted,"How much world fits across the square. The scale stays 1:1: pixels per metre is the same on both axes, so distances and directions read true.",ImGui::GetContentRegionAvail().x);
    { char buf[96]; float ppm=s.radar_square_px/std::max(1.f,s.radar_area_m);
      snprintf(buf,sizeof(buf),"%.2f px per metre",ppm); InfoStrip("Scale",buf); }
    Divider();
    SubLabel("Orientation");
    Label1("Turn with my view"); ToggleRight("##radarrotate",&s.radar_rotate_with_camera);
    WrapText(kMuted,"On: up on the radar is always where you are looking, so turning the camera turns the square. The facing is read from the game's view matrix. Off: the square stays world aligned, north up.",ImGui::GetContentRegionAvail().x);
    SliderRowF("Extra rotation","##radaryaw",&s.radar_yaw,-3.15f,3.15f,"%.2f rad","");
    WrapText(kMuted,"Added on top of the camera facing. Leave at 0 unless the square reads rotated for you.",ImGui::GetContentRegionAvail().x);
    Divider();
    SubLabel("Marks");
    Label1("Movement direction"); ToggleRight("##radartrack",&s.radar_track);
    WrapText(kMuted,"A short tick points the way a player is moving, from their last two positions.",ImGui::GetContentRegionAvail().x);
    ColorRow("On screen · colour","radardot",s.radar_dot_colour,true);
    WrapText(kMuted,"Filled square: the game is drawing that body right now, so it is in front of you.",ImGui::GetContentRegionAvail().x);
    ColorRow("Behind / off camera · colour","radarwall",s.radar_wall_colour,true);
    WrapText(kMuted,"Hollow square: not currently drawn by the game, so behind you, beside you, or behind a wall. Held for about four seconds after it was last seen.",ImGui::GetContentRegionAvail().x);
    ColorRow("Frame · colour","radarframe",s.radar_frame_colour,true);
    ColorRow("Background · colour","radarbg",s.radar_bg_colour,true);
    Divider();
    { char buf[128]; snprintf(buf,sizeof(buf),"%u player%s on the radar",chams::radar_marks_now(),chams::radar_marks_now()==1?"":"s");
      InfoStrip("Live",buf); }
    EndFigmaSection();
  }

  // ================= 05 2D stroke =================
  if(BeginFigmaSection("stroke","05","2D stroke","A clean screen-space outline around detected characters.",&s.stroke)){
    SliderRowI("Stroke size · open area","##stopen",&s.stroke_open_size,1,16,"px");
    SliderRowI("Stroke size · covered area","##stthrough",&s.stroke_through_size,1,16,"px");
    SliderRowF("Stroke strength","##stalpha",&s.stroke_alpha,0.05f,1.f,"%.2f","");
    Divider();
    ColorRow("Open part · colour","stopen",s.stroke_open_color,false);
    ColorRow("Through walls · colour","stthrough",s.stroke_through_color,false);
    Label1("Open part enabled"); ToggleRight("##stopen_t",&s.stroke_open);
    EndFigmaSection();
  }

  // ================= 06 3D wireframe =================
  if(BeginFigmaSection("wire","06","3D wireframe","Trace character geometry with a fine, three-dimensional mesh.",&s.wire)){
    SliderRowI("Wire size","##wsize",&s.wire_size,1,8,"px");
    SliderRowF("Wire strength","##walpha",&s.wire_alpha,0.05f,1.f,"%.2f","");
    Divider();
    ColorRow("Through walls · colour","wirethrough",s.wire_through_color,false);
    ColorRow("Open part · colour","wireopen",s.wire_open_color,false);
    Label1("Open part enabled"); ToggleRight("##wireopen_t",&s.wire_open);
    if(!s.wire) InfoStrip("Wireframe is off","Enable 3D wireframe stroke in the header of this panel to apply these settings.");
    EndFigmaSection();
  }

  // ================= Logs =================
  {
    bool &open=g_open["logs"];
    float k=Unit(); float w=ImGui::GetContentRegionAvail().x; ImVec2 cur=ImGui::GetCursorScreenPos(); float r=Rounding();
    float pad=12*k; float th=ImGui::GetTextLineHeight(); float h=std::max(44*k,pad*2+th);
    ImGui::PushID("logs");
    bool pressed=ImGui::InvisibleButton("##hdr_logs",ImVec2(w,h));
    bool hov=ImGui::IsItemHovered();
    if(pressed) open=!open;
    ImDrawList* dl=ImGui::GetWindowDrawList();
    dl->AddRectFilled(cur,ImVec2(cur.x+w,cur.y+h),Col(hov?Mix(kCard,kText,0.03f):kCard),r);
    dl->AddRect(cur,ImVec2(cur.x+w,cur.y+h),Col(kLine),r,0,Hair());
    Chevron(dl,ImVec2(cur.x+14*k,cur.y+h*0.5f),open,open?kMint:kDim);
    ClipText(dl,ImVec2(cur.x+28*k,cur.y+(h-th)*0.5f),w-120*k,kText,"Logs");
    { const char* rec="0 records"; float rw=TextW(rec); dl->AddText(ImVec2(cur.x+w-pad-rw,cur.y+(h-th)*0.5f),Col(kDim),rec); }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(cur.x,cur.y+h+7*k));
    if(open){
      ImGui::PushStyleColor(ImGuiCol_ChildBg,kCard);
      ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,r);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(12*k,10*k));
      ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,Hair());
      ImGui::BeginChild("logs_body",ImVec2(w,0),ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY);
      static std::vector<std::string> lines; static bool loadedLogs=false;
      if(!loadedLogs){ std::ifstream f(chams::dump_path()); if(f.is_open()){ std::string l; while(std::getline(f,l)) lines.push_back(l); if(lines.size()>400) lines.erase(lines.begin(),lines.end()-400);} loadedLogs=true; }
      if(ImGui::Button("Refresh")){ lines.clear(); loadedLogs=false; }
      ImGui::SameLine();
      if(ImGui::Button("Copy path")){ ImGui::SetClipboardText(chams::dump_path()); }
      ImGui::SameLine();
      if(ImGui::Button("Open folder")){ char p[MAX_PATH*4]={}; GetTempPathA(sizeof(p),p); std::string c="explorer \""+std::string(p)+"\""; system(c.c_str()); }
      ImGui::BeginChild("chamslogscroll",ImVec2(0,140*k),ImGuiChildFlags_Borders);
      int start=(int)lines.size()>24?(int)lines.size()-24:0;
      for(int i=start;i<(int)lines.size();++i){ ImGui::PushStyleColor(ImGuiCol_Text,kDim); ImGui::TextUnformatted(lines[i].c_str()); ImGui::PopStyleColor(); }
      if(lines.empty()) ImGui::TextDisabled("No dump yet. Enable Dump mode in Runtime capture.");
      ImGui::EndChild();
      ImGui::EndChild(); ImGui::PopStyleVar(3); ImGui::PopStyleColor(1);
      ImGui::Dummy(ImVec2(0,7*k));
    }
  }
  FooterHazu();
  ImGui::PopStyleVar(1);
  ImGui::PopStyleColor(4);
}
#endif
