#!/usr/bin/env python3
"""Compile the actual inline GL code against a small stateful mock (not a driver test)."""
import pathlib, re, subprocess, tempfile, json
root = pathlib.Path(__file__).resolve().parents[1]
s = (root/'source/chams_gl.inl').read_text() + (root/'source/chams_overlay.inl').read_text()
tokens = sorted(set(re.findall(r'\bGL_[A-Z0-9_]+\b', s)) - {'GL_TRUE','GL_FALSE'})
names = sorted(set(re.findall(r'\bgl\.([A-Za-z0-9_]+)', s)))
custom = {'GetUniformLocation','GetUniformfv','GetIntegerv','GetBooleanv','GetFloatv','IsEnabled','Enable','Disable','ColorMask','DepthMask','DepthFunc','DepthRange','BlendFuncSeparate','BlendFunc','BlendEquationSeparate','BlendEquation','BlendColor','LineWidth','PolygonMode','PolygonOffset','Scissor','ActiveTexture','BindTexture','BindFramebuffer','UseProgram','BindVertexArray','BindBuffer','Viewport','ClearColor'}
head = '''#include <cstddef>
#include <map>
#include <array>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
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
int uniform_location(unsigned program,const char *name){
 if(program==123 && std::strcmp(name,"_DiffIntensity")==0) return 7;
 if(program==456 && std::strcmp(name,"_CharaLightIntensity")==0) return 8;
 return -1;
}
void get_uniform(unsigned, int, float *out){for(int i=0;i<16;++i)out[i]=float(i+1);}
struct GL {
'''
head += ''.join('Any '+n+';\n' for n in names if n not in custom)
head += '''
decltype(&uniform_location) GetUniformLocation=uniform_location;
decltype(&get_uniform) GetUniformfv=get_uniform;
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
std::vector<std::string> captured_lines;
namespace reshade::chams::capture { uintptr_t native_context(){ return 1; } uintptr_t native_surface(){ return 2; } }
namespace reshade::chams { settings g_settings; bool g_overlay_open=false; int g_budget_left=110; unsigned g_frame_id=1; void dump_line(const char *line){ captured_lines.emplace_back(line); } void ensure_loaded(){} }
namespace reshade::log { void message(level,const char*,...){} }
int main(int argc, char **argv) {
 assert(argc==2);
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
 // Reused cache slots must not retain another program's matrices/camera.
 icache().count=0;program_info *old=get_program_info(123);assert(old);
 old->world[12]=7;old->view_m[13]=9;old->cam_uniform[0]=11;
 icache().count=0;program_info *fresh=get_program_info(124);assert(fresh);
 assert(fresh->world[12]==0 && fresh->view_m[13]==0 && fresh->cam_uniform[0]==0);
 // Clip depth must not be derived from an unavailable object transform.
 program_info p{};p.view_proj.plain=0;p.vp_m[15]=4;
 assert(object_distance(p)<0);
 p.obj_to_world.plain=0;p.world[12]=1;assert(object_distance(p)==4);
 // If a declared camera uniform has zero data, name the actual matrix fallback.
 p.view.plain=0;p.view_m[0]=p.view_m[5]=p.view_m[10]=1;p.view_m[12]=-2;
 p.loc_cam_pos=0;p.world[12]=5;assert(object_distance(p)==3);
 assert(std::strcmp(p.dist_source,"MATRIX_V")==0);
 // A driver returning 16 floats for every array location must not overwrite
 // the adjacent capture object; it may be an array of matrices, not vectors.
 struct { float value[16]; int sentinel; } safe{{},12345}; matrix_loc arrayloc{};arrayloc.array_style=true;
 for(int i=0;i<4;i++) arrayloc.loc[i]=i;
 capture_read_matrix(77,arrayloc,safe.value); assert(safe.sentinel==12345);
 assert(safe.value[0]==1 && safe.value[4]==1 && safe.value[15]==4);
 // Projected origin is viewport-relative and fails when matrix/clip w is missing.
 program_info projection{};projection.obj_to_world.plain=0;projection.view_proj.plain=0;
 projection.world[12]=.5f;projection.vp_m[0]=projection.vp_m[5]=projection.vp_m[15]=1;
 int viewport[4]={10,20,800,600};float screen[2]={};
 assert(diagnostic_project_origin(projection,viewport,screen));
 assert(screen[0]==610 && screen[1]==320);projection.vp_m[15]=0;
 assert(!diagnostic_project_origin(projection,viewport,screen));
 // Actual wrapper: budget-exhausted calls remain exactly one original draw,
 // with a per-frame summary only when Dump ALL is enabled.
 captured_lines.clear();g_settings=settings{};g_settings.dump_all=true;
 g_opengl_context=reinterpret_cast<void *>(1);g_budget_left=0;g_frame_id=100;
 int originals=0;draw_chams(GL_TRIANGLES,200,[&]{++originals;});
 assert(originals==1 && captured_lines.empty());
 g_frame_id=101;draw_chams(GL_TRIANGLES,200,[&]{++originals;});
 assert(originals==2 && captured_lines.size()==1);
 assert(captured_lines[0].find("[CHAMS-FRAME] f=100")!=std::string::npos);
 assert(captured_lines[0].find("calls=1")!=std::string::npos && captured_lines[0].find("budget=1")!=std::string::npos);
 g_settings.dump_all=false;g_frame_id=102;draw_chams(GL_TRIANGLES,200,[&]{++originals;});
 assert(originals==3 && captured_lines.size()==1);
 // Disabling each dump re-arms it even with an exhausted draw budget.
 dumpall_was_on()=true;dumpall_lines_left()=0;dump_was_on()=true;dump_lines_left()=0;
 g_settings.dump_all=false;g_settings.dump=false;
 draw_chams(GL_TRIANGLES,200,[&]{++originals;});
 assert(!dumpall_was_on() && dumpall_lines_left()==3000);
 assert(!dump_was_on() && dump_lines_left()==2000);
 // New diagnostic context and hints appear only for an explicitly enabled dump.
 captured_lines.clear();g_settings.dump=true;float camera[3]={1,2,3};
 dump_draw(123,100,projection,3.0f,true,false,camera);
 assert(captured_lines.size()==2);
 assert(captured_lines.back().find("ctx=")!=std::string::npos);
 assert(captured_lines.back().find("wall_bits=0")!=std::string::npos);
 assert(captured_lines.back().find("origin_valid=0")!=std::string::npos);
 assert(captured_lines.back().find("element_buffer=")!=std::string::npos);
 g_settings.dump=false;dump_was_on()=false;
 // Summaries are bounded and segmented when the context changes.
 frame_trace bounded;int emitted=0;auto emit=[&](const char*){++emitted;};
 for(unsigned i=0;i<1900;i++) assert(bounded.enter(true,reinterpret_cast<void *>(1),i,1,1,false,emit));
 assert(emitted==1800);
 assert(!bounded.enter(false,nullptr,1900,1,1,false,emit));
 assert(bounded.enter(true,reinterpret_cast<void *>(1),1901,0,1,true,emit));
 assert(bounded.enter(true,reinterpret_cast<void *>(2),1901,0,1,true,emit));
 assert(emitted==1801);
 // Overlay mode state is shared across different draw-wrapper lambdas.
 g_settings.dump_all=false;g_settings.enabled=true;g_settings.render_mode=1;
 g_overlay_open=false;g_budget_left=3;g_frame_id=105;previous_render_mode()=-1;
 draw_chams(GL_TRIANGLES,200,[&]{});
 assert(previous_render_mode()==1);ov().has_mask=true;ov().captured_frame=105;
 draw_chams(GL_TRIANGLES,200,[&]{});
 assert(ov().has_mask && ov().captured_frame==105);
 // An open/close transition is observed even when the budget is exhausted.
 g_budget_left=0;g_overlay_open=true;
 draw_chams(GL_TRIANGLES,200,[&]{});
 assert(ov().has_mask);g_overlay_open=false;
 draw_chams(GL_TRIANGLES,200,[&]{});
 assert(!ov().has_mask && ov().captured_frame==0xFFFFFFFFu);
 // Exercise actual integrated scope against the standalone asynchronous writer.
 assert(capture::runtime_start(argv[1]));
 g_opengl_context=reinterpret_cast<void *>(1);g_settings.enabled=true;g_overlay_open=false;g_budget_left=0;
 ints[GL_CURRENT_PROGRAM][0]=123; ints[GL_ELEMENT_ARRAY_BUFFER_BINDING][0]=805;
 { capture_gl_scope scope("mock_draw", GL_TRIANGLES, 200); scope.index_pointer(reinterpret_cast<void *>(40)); draw_chams(GL_TRIANGLES,200,[&]{}, &scope); }
 g_budget_left=110;g_settings.detect_mode=0;g_settings.render_mode=1;g_settings.glow=true;
 g_settings.glow_alpha=.5f;g_settings.stroke=false;g_settings.wire=false;
 { capture_gl_scope scope("reject_draw", GL_TRIANGLES, 200); draw_chams(GL_TRIANGLES,200,[&]{}, &scope); }
 ints[GL_CURRENT_PROGRAM][0]=456;
 { capture_gl_scope scope("accepted_draw", GL_TRIANGLES, 200); draw_chams(GL_TRIANGLES,200,[&]{}, &scope); }
 capture::runtime_stop(); auto status=capture::runtime_status();
 assert(!status.io_error && !status.dropped && status.draws==3 && status.instrumentation_samples==3);
 // Program metadata must be re-emitted when another session starts.
 assert(capture::runtime_start(argv[1]));
 g_budget_left=0;ints[GL_CURRENT_PROGRAM][0]=123;ints[GL_ELEMENT_ARRAY_BUFFER_BINDING][0]=0;
 { capture_gl_scope scope("mock_draw", GL_TRIANGLES, 200); scope.index_pointer(reinterpret_cast<void *>(0xdeadbeef)); draw_chams(GL_TRIANGLES,200,[&]{}, &scope); }
 capture::runtime_stop(); status=capture::runtime_status();
 assert(!status.io_error && !status.dropped && status.draws==1);
 const int iterations=4000; auto off_start=std::chrono::steady_clock::now();
 for(int i=0;i<iterations;i++){ capture_gl_scope scope("benchmark", GL_TRIANGLES, 200); draw_chams(GL_TRIANGLES,200,[&]{}, &scope); }
 const auto off_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-off_start).count();
 assert(capture::runtime_start(argv[1])); auto on_start=std::chrono::steady_clock::now();
 for(int i=0;i<iterations;i++){ capture_gl_scope scope("benchmark", GL_TRIANGLES, 200); draw_chams(GL_TRIANGLES,200,[&]{}, &scope); }
 const auto on_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-on_start).count();
 capture::runtime_stop(); status=capture::runtime_status();
 assert(!status.io_error && !status.dropped && status.draws==iterations && status.instrumentation_samples==iterations);
 std::printf("MOCK BENCHMARK: off %.1f ns/draw, on %.1f ns/draw (not driver or in-game FPS)\n", double(off_ns)/iterations,double(on_ns)/iterations);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=pathlib.Path(d)/'mock.cpp';p.write_text(head);exe=pathlib.Path(d)/'test'
 subprocess.run(['g++','-std=c++17','-O0','-Wformat=2','-Werror=format',str(p),str(root/'source/chams_capture.cpp'),'-pthread','-o',str(exe)],check=True)
 subprocess.run([str(exe),d],check=True)
 sessions=list(pathlib.Path(d).glob('session-*')); assert len(sessions)==3
 observed=[]; offsets=[]
 for session in sessions:
  events=[json.loads(line) for f in sorted(session.glob('capture-*.jsonl')) for line in f.read_text().splitlines()]
  draws=[e for e in events if e['event']=='draw']
  infos=[e for e in events if e['event']=='program_info']; assert infos
  first=draws[0]; assert first['api'] in ('mock_draw','benchmark') and first['decision']=='budget' and first['count']==200
  assert first['program_epoch']==infos[0]['epoch'] and first['program']==infos[0]['program']
  assert first['color_mask']==[1,0,1,0] and len(first['depth_range'])==2
  assert 'scissor_box' in first and 'blend_src_alpha' in first and 'texture_2d' in first
  if first['api']=='benchmark': assert len(draws)==4000
  else: observed.append([x['decision'] for x in draws]); offsets.append((first['has_index_offset'],first['index_offset']))
 assert sorted(observed,key=len)==[['budget'],['budget','classifier_reject','direct']]
 assert sorted(offsets)==[(0,0),(1,40)] # client-memory addresses are never recorded
print('PASS: GL state round trips; tracker interrupted sequences; Strict/Wide cache mode switch; full inline code compiled against mock GL')
