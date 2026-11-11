#!/usr/bin/env python3
"""Compile the actual inline GL code against a small stateful mock (not a driver test)."""
import pathlib, re, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
s = (root/'source/chams_gl.inl').read_text() + (root/'source/chams_overlay.inl').read_text()
tokens = sorted(set(re.findall(r'\bGL_[A-Z0-9_]+\b', s)) - {'GL_TRUE','GL_FALSE'})
names = sorted(set(re.findall(r'\bgl\.([A-Za-z0-9_]+)', s)))
custom = {'GetUniformLocation','GetIntegerv','GetBooleanv','GetFloatv','IsEnabled','Enable','Disable','ColorMask','DepthMask','DepthFunc','DepthRange','BlendFuncSeparate','BlendFunc','BlendEquationSeparate','BlendEquation','BlendColor','LineWidth','PolygonMode','PolygonOffset','Scissor','ActiveTexture','BindTexture','BindFramebuffer','UseProgram','BindVertexArray','BindBuffer','Viewport','ClearColor'}
head = '''#include <cstddef>
#include <map>
#include <array>
#include <cassert>
#include <cstring>
using GLenum=unsigned; using GLuint=unsigned; using GLint=int; using GLsizei=int; using GLboolean=unsigned char; using GLfloat=float;
#define GL_FALSE 0
#define GL_TRUE 1
'''+''.join('#define '+x+' '+str(i+2)+'\n' for i,x in enumerate(tokens))
head += r'''
struct Any { template<class... T> int operator()(T...) const { return 0; } bool operator==(std::nullptr_t) const { return false; } bool operator!=(std::nullptr_t) const { return true; } explicit operator bool() const { return true; } };
std::map<unsigned,std::array<int,4>> ints;
std::map<unsigned,std::array<float,4>> floats;
std::map<unsigned,bool> enabled;
std::map<unsigned,int> textures;
void gi(unsigned k,int* p) { auto v=ints[k]; int n=(k==GL_VIEWPORT||k==GL_SCISSOR_BOX)?4:(k==GL_POLYGON_MODE?2:1); if(k==GL_TEXTURE_BINDING_2D) v[0]=textures[ints[GL_ACTIVE_TEXTURE][0]]; for(int i=0;i<n;i++) p[i]=v[i]; }
void gb(unsigned k,unsigned char* p) { auto v=ints[k]; int n=k==GL_COLOR_WRITEMASK?4:1; for(int i=0;i<n;i++) p[i]=v[i]; }
void gf(unsigned k,float* p) { auto v=floats[k]; int n=(k==GL_BLEND_COLOR||k==GL_COLOR_CLEAR_VALUE)?4:(k==GL_DEPTH_RANGE?2:1); for(int i=0;i<n;i++)p[i]=v[i]; }
bool is(unsigned k){return enabled[k];} void en(unsigned k){enabled[k]=true;} void dis(unsigned k){enabled[k]=false;}
void cm(int a,int b,int c,int d){ints[GL_COLOR_WRITEMASK]={a,b,c,d};} void dm(int a){ints[GL_DEPTH_WRITEMASK][0]=a;}
void df(unsigned x){ints[GL_DEPTH_FUNC][0]=x;} void dr(float x,float y){floats[GL_DEPTH_RANGE]={x,y,0,0};}
void bfs(unsigned a,unsigned b,unsigned c,unsigned d){ints[GL_BLEND_SRC_RGB][0]=a;ints[GL_BLEND_DST_RGB][0]=b;ints[GL_BLEND_SRC_ALPHA][0]=c;ints[GL_BLEND_DST_ALPHA][0]=d;}
void bf(unsigned a,unsigned b){bfs(a,b,a,b);} void bes(unsigned a,unsigned b){ints[GL_BLEND_EQUATION_RGB][0]=a;ints[GL_BLEND_EQUATION_ALPHA][0]=b;} void be(unsigned a){bes(a,a);}
void bc(float a,float b,float c,float d){floats[GL_BLEND_COLOR]={a,b,c,d};} void lw(float a){floats[GL_LINE_WIDTH][0]=a;}
void pm(unsigned,unsigned x){ints[GL_POLYGON_MODE]={int(x),int(x),0,0};} void po(float a,float b){floats[GL_POLYGON_OFFSET_FACTOR][0]=a;floats[GL_POLYGON_OFFSET_UNITS][0]=b;}
void sc(int a,int b,int c,int d){ints[GL_SCISSOR_BOX]={a,b,c,d};} void at(unsigned a){ints[GL_ACTIVE_TEXTURE][0]=a;} void bt(unsigned,unsigned a){textures[ints[GL_ACTIVE_TEXTURE][0]]=a;}
void fb(unsigned target,unsigned x){if(target==GL_FRAMEBUFFER||target==GL_DRAW_FRAMEBUFFER)ints[GL_DRAW_FRAMEBUFFER_BINDING][0]=x;if(target==GL_FRAMEBUFFER||target==GL_READ_FRAMEBUFFER)ints[GL_READ_FRAMEBUFFER_BINDING][0]=x;}
void up(unsigned x){ints[GL_CURRENT_PROGRAM][0]=x;} void va(unsigned x){ints[GL_VERTEX_ARRAY_BINDING][0]=x;} void bb(unsigned,unsigned x){ints[GL_ARRAY_BUFFER_BINDING][0]=x;}
void vp(int a,int b,int c,int d){ints[GL_VIEWPORT]={a,b,c,d};} void cc(float a,float b,float c,float d){floats[GL_COLOR_CLEAR_VALUE]={a,b,c,d};}
int uniform_location(unsigned program,const char *name){return program==123 && std::strcmp(name,"_DiffIntensity")==0 ? 7 : -1;}
struct GL {
'''
head += ''.join('Any '+n+';\n' for n in names if n not in custom)
head += '''
decltype(&uniform_location) GetUniformLocation=uniform_location;
decltype(&gi) GetIntegerv=gi; decltype(&gb) GetBooleanv=gb; decltype(&gf) GetFloatv=gf;
decltype(&is) IsEnabled=is; decltype(&en) Enable=en; decltype(&dis) Disable=dis;
decltype(&cm) ColorMask=cm; decltype(&dm) DepthMask=dm; decltype(&df) DepthFunc=df; decltype(&dr) DepthRange=dr;
decltype(&bfs) BlendFuncSeparate=bfs; decltype(&bf) BlendFunc=bf; decltype(&bes) BlendEquationSeparate=bes; decltype(&be) BlendEquation=be; decltype(&bc) BlendColor=bc;
decltype(&lw) LineWidth=lw; decltype(&pm) PolygonMode=pm; decltype(&po) PolygonOffset=po; decltype(&sc) Scissor=sc;
decltype(&at) ActiveTexture=at; decltype(&bt) BindTexture=bt; decltype(&fb) BindFramebuffer=fb; decltype(&up) UseProgram=up;
decltype(&va) BindVertexArray=va; decltype(&bb) BindBuffer=bb; decltype(&vp) Viewport=vp; decltype(&cc) ClearColor=cc;
} gl;
void *g_opengl_context=nullptr;
'''
head += '#include "'+str(root/'source/chams_gl.inl')+'"\n'
head += r'''
namespace reshade::chams { settings g_settings; bool g_overlay_open=false; int g_budget_left=110; unsigned g_frame_id=1; void dump_line(const char*){} void ensure_loaded(){} }
namespace reshade::log { void message(level,const char*,...){} }
int main() {
 using namespace reshade::chams;
 en(GL_DEPTH_TEST); en(GL_SCISSOR_TEST); en(GL_CULL_FACE); dm(1); cm(1,0,1,0); df(GL_GREATER); dr(.2f,.8f);
 bfs(5,6,7,8); bes(9,10); bc(.1f,.2f,.3f,.4f); lw(3); pm(0,GL_FILL); po(2,3); sc(2,3,4,5);
 at(GL_TEXTURE0); bt(GL_TEXTURE_2D,101); at(GL_TEXTURE0+500); bt(GL_TEXTURE_2D,102); fb(GL_DRAW_FRAMEBUFFER,13); fb(GL_READ_FRAMEBUFFER,14); up(15);va(16);bb(0,17);vp(1,2,800,600);cc(.2f,.4f,.6f,.8f);
 (void)save_state(); { overlay_state_guard warmup; }
 auto original_i=ints; auto original_f=floats; auto original_e=enabled; auto original_t=textures;
 { overlay_state_guard guard; at(GL_TEXTURE0);bt(GL_TEXTURE_2D,88);at(GL_TEXTURE0+500);bt(GL_TEXTURE_2D,99);fb(GL_FRAMEBUFFER,20);up(21);va(22);bb(0,23);vp(0,0,4,5);cc(1,1,1,1); float col[3]={1,0,0}; begin_tint(col,.5f);lw(9); }
 assert(ints==original_i); assert(floats==original_f); assert(enabled==original_e); assert(textures==original_t);
 auto st=save_state(); float col[3]={1,0,0}; wire_pass(GL_LESS,col,.5f,7.0f,[]{});restore_state(st);
 assert(ints==original_i);assert(floats==original_f);assert(enabled==original_e);
 own()=own_track{}; for(int i=0;i<30;i++){own().frame_min=2.17f;update_own_estimate();} assert(own().stable==30);assert(own().own_est>2.16f&&own().own_est<2.18f);
 update_own_estimate(); assert(own().own_est<0 && own().stable==0);
 own()=own_track{}; for(int i=0;i<29;i++){own().frame_min=2.17f;update_own_estimate();}
 own().frame_min=1.0f; update_own_estimate(); assert(own().stable==0 && own().own_est<0);
 own().frame_min=2.17f;update_own_estimate();assert(own().stable==1 && own().own_est<0);
 for(int i=1;i<30;i++){own().frame_min=2.17f;update_own_estimate();}
 assert(own().own_est>0);own().frame_min=8.0f;update_own_estimate();assert(own().own_est<0 && own().stable==0);
 pcache().count=0;g_settings.detect_mode=0;assert(!program_is_character(123));
 g_settings.detect_mode=1;assert(program_is_character(123));
 g_settings.detect_mode=0;assert(!program_is_character(123));
}
'''
with tempfile.TemporaryDirectory() as d:
 p=pathlib.Path(d)/'mock.cpp';p.write_text(head);exe=pathlib.Path(d)/'test'
 subprocess.run(['g++','-std=c++17','-O0',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS: GL state round trips; tracker interrupted sequences; Strict/Wide cache mode switch; full inline code compiled against mock GL')
