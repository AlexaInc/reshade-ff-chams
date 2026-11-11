#!/usr/bin/env python3
"""Compile the actual inline GL code against a small stateful mock (not a driver test)."""
import pathlib, re, subprocess, tempfile, json
root = pathlib.Path(__file__).resolve().parents[1]
fixture=json.loads((root/'tests/fixtures/f9-000005-selected.json').read_text())
assert fixture['source_sha256']=='723d3fdb5a304436f6227d1e29c32abd986a14918576c634d02dda9272e1fd24'
events={e['seq']:e for e in fixture['events']}
assert events[45707]['event']=='marker' and events[45707]['context']==1
assert events[45738]['world']==events[45740]['world'] and events[45738]['view_projection']==events[45740]['view_projection']
assert events[68037]['world']==events[68040]['world'] and events[68037]['view_projection']==events[68040]['view_projection']
assert (events[68037]['decision'],events[68040]['decision'])==('direct','hide_own')
assert events[69236]['event']=='present_begin' and events[69236]['context']==1 and events[69239]['context']==3
assert events[69239]['reason']=='no_observation' and events[264480]['reason']=='out_of_range'
flicker=json.loads((root/'tests/fixtures/flicker-8ea3d22-selected.json').read_text())
assert flicker['source_sha256']=='09ac10364b6eb4ec86ec89219b3b48e79135ad38e7ca463d231df3632627bf26'
fl={e['line']:e for e in flicker['records']}
assert [fl[n]['hidden'] for n in (8683,9073,9233)]==[0,1,0]
assert [fl[n]['stable'] for n in (8683,9073,9233)]==[0,30,0]
assert [fl[n]['ownest'] for n in (8683,9073,9233)]==[-1.0,2.63,-1.0]
assert fl[8683]['world']==fl[9073]['world']==fl[9233]['world']
assert fl[10559]['hidden']==fl[10570]['hidden']==0
assert fl[10560]['hidden']==0 and fl[10571]['hidden']==1
assert fl[10559]['f']==fl[10560]['f'] and fl[10570]['f']==fl[10571]['f']
assert all(e['hideown']==1 for e in fl.values())
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
thread_local uintptr_t mock_native_context=0;
namespace reshade::chams::capture { uintptr_t native_context(){ return mock_native_context ? mock_native_context : reinterpret_cast<uintptr_t>(g_opengl_context); } uintptr_t native_surface(){ return 2; } }
namespace reshade::chams { settings g_settings; bool g_overlay_open=false; int g_budget_left=110; std::atomic<unsigned> g_frame_id{1}; void dump_line(const char *line){ captured_lines.emplace_back(line); } void ensure_loaded(){} }
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
 // Captured Gloo School pair seq 33655/33657: same object and VP, but
 // only one program supplies _WorldSpaceCameraPos. Compare Euclidean distances.
 program_info captured{};captured.obj_to_world.plain=0;captured.view_proj.plain=0;
 const float vp_capture[16]={.950513542f,.156456828f,.198121071f,.198117122f,1.9591117e-7f,1.57596874f,-.414866656f,-.414858401f,-.21205093f,.701315761f,.888072968f,.888055265f,-42.7277451f,422.214752f,106.238464f,106.436348f};
 std::memcpy(captured.vp_m,vp_capture,sizeof(vp_capture));
 captured.world[12]=-.145164028f;captured.world[13]=-179.9702f;captured.world[14]=-200.476898f;
 captured.loc_cam_pos=0;captured.cam_uniform[0]=-.285238892f;captured.cam_uniform[1]=-177.643066f;captured.cam_uniform[2]=-202.776306f;
 const float uniform_distance=object_distance(captured);captured.loc_cam_pos=-1;
 const float vp_distance=object_distance(captured);assert(std::fabs(uniform_distance-vp_distance)<.001f);
 assert(std::strcmp(captured.dist_source,"MATRIX_VP_CAMERA")==0);
 float camtest[3]={}; assert(!perspective_camera(p.vp_m,camtest)); // orthographic/singular
 captured.vp_m[0]=NAN;assert(!perspective_camera(captured.vp_m,camtest));
 // Stationary capture seq 55062/55064: same fixed object/VP, different
 // programmes used Euclidean 3.486254 and axial depth 3.329197 respectively.
 const float vp_stationary[16]={-.865868151f,-.509609222f,-.350648344f,-.35064137f,-1.45119383e-8f,1.326828f,-.64280045f,-.642787635f,.445773721f,-.989861667f,-.68109709f,-.68108356f,52.8365974f,39.3694f,49.1876106f,49.3866272f};
 program_info stationary{};stationary.obj_to_world.plain=0;stationary.view_proj.plain=0;
 std::memcpy(stationary.vp_m,vp_stationary,sizeof(vp_stationary));
 stationary.world[12]=71.637001f;stationary.world[13]=11.4359999f;stationary.world[14]=19.9500008f;
 stationary.loc_cam_pos=0;stationary.cam_uniform[0]=72.2411652f;stationary.cam_uniform[1]=14.3329706f;stationary.cam_uniform[2]=21.7929649f;
 const float stationary_uniform=object_distance(stationary);stationary.loc_cam_pos=-1;
 assert(std::fabs(object_distance(stationary)-stationary_uniform)<.001f);
 assert(std::fabs(stationary_uniform-3.48625445f)<.001f);
 assert(std::strcmp(stationary.dist_source,"MATRIX_VP_CAMERA")==0);
 // Context changes on one thread cannot reuse another context's estimate or
 // program cache, even if global present epoch did not change.
 g_opengl_context=reinterpret_cast<void *>(1);g_frame_id=70;touch_frame();
 own().own_est=3.15794563f;own().stable=30;pcache().count=1;icache().count=1;
 g_opengl_context=reinterpret_cast<void *>(2);touch_frame();
 assert(own().own_est<0 && own().stable==0 && pcache().count==0 && icache().count==0);
 // The WGL implementation reuses the same device wrapper for contexts in
 // a share group. A distinct native context is not a distinct wrapper pointer.
 // A game draw must not inherit the previous native context's own estimate.
 g_opengl_context=reinterpret_cast<void *>(0x8000);mock_native_context=0x1000;touch_frame();
 own().own_est=2.74557114f;own().stable=48;pcache().count=1;icache().count=1;
 touch_frame(); // no actual context change: preserve this context's state
 assert(own().own_est>0 && own().stable==48 && pcache().count==1 && icache().count==1);
 mock_native_context=0x2000;touch_frame();
 assert(own().own_est<0 && own().stable==0 && pcache().count==0 && icache().count==0);
 // Returning to the earlier native context is conservatively fresh, not a
 // stale estimate from either context. No persistent migration is implied.
 own().own_est=3.1f;own().stable=30;mock_native_context=0x1000;touch_frame();
 assert(own().own_est<0 && own().stable==0);
 // Even if the native handle remains identical, replacing the wrapper is
 // conservative evidence that the previous per-thread cache should reset.
 own().own_est=3.1f;own().stable=30;g_opengl_context=reinterpret_cast<void *>(0x8001);touch_frame();
 assert(own().own_est<0 && own().stable==0);
 mock_native_context=0;
 // Different render threads must not share the global-own estimate as in
 // stationary seq 153699/153710. This checks isolation, not real GL timing.
 own().own_est=2.99280167f;own().stable=30;pcache().count=1;
 std::thread other([] { using namespace reshade::chams; assert(own().own_est<0 && own().stable==0);assert(pcache().count==0);own().own_est=7;own().stable=1;pcache().count=2; });
 other.join();assert(own().own_est==2.99280167f && own().stable==30 && pcache().count==1);
 g_opengl_context=nullptr;own()=own_track{};pcache().count=0;icache().count=0;
 // F9 capture 000005 is a documented failure reproduction, NOT a fix test.
 // The same shared-root candidate was direct at seq 45738/45740 while a
 // different-origin part 45742 was hidden. No assertion about actual pixel
 // ownership is made here; capture data alone cannot prove that association.
 settings f9{}; f9.hide_own=true; f9.own_distance=5.30000019f;
 program_info f9_info{}; f9_info.dist_source="_WorldSpaceCameraPos";
 own_track f9_state{}; f9_state.own_est=@F9_EST@; f9_state.stable=60;
 auto pending = [](const char *name, bool satisfied) {
  std::printf("%s: %s\n", satisfied ? "XPASS (promote to regression)" : "XFAIL (known unfixed)", name);
 };
 const bool f9_body=own_distance_excludes(f9,f9_info,@F9_BODY@,f9_state);
 const bool f9_part=own_distance_excludes(f9,f9_info,@F9_PART@,f9_state);
 // This is conditional on the suspected association, not established ground truth.
 pending("shared-root body and putative part get consistent decisions",f9_body==f9_part);
 // Same-world/VP seq 68037/68040: the only decision input changed was
 // the tracker estimate mid-group. Capture distance is approximately 2.964086.
 f9_state.own_est=@F9_BEFORE@;const bool before=own_distance_excludes(f9,f9_info,@F9_BODY@,f9_state);
 f9_state.own_est=@F9_AFTER@;const bool after=own_distance_excludes(f9,f9_info,@F9_BODY@,f9_state);
 pending("identical shared-root draw unaffected by mid-group estimate update", before==after);
 // Nearby-opponent negative is deliberately synthetic: the current
 // distance-only predicate cannot differentiate equal-distance identities.
 const bool synthetic_own=own_distance_excludes(f9,f9_info,2.9f,f9_state);
 const bool synthetic_nearby_enemy=own_distance_excludes(f9,f9_info,2.9f,f9_state);
 pending("hypothetical equal-distance nearby opponent stays visible", synthetic_own && !synthetic_nearby_enemy);
 // Exact captured interval 69236/69239: an unrelated context's present
 // increments the global epoch and the next game touch loses its estimate.
 g_opengl_context=reinterpret_cast<void *>(3);g_frame_id=180;touch_frame();
 own().own_est=@F9_RESET@;own().stable=48;own().frame_min=1e9f;
 ++g_frame_id;touch_frame();
 pending("unrelated-context present does not reset active game tracking",own().own_est>0 && own().stable==48);
 // Exact partial interval 264475/264477/264480: nearer candidate follows
 // a present but is not in the artificial interval's 5.164870 minimum.
 own().own_est=@F9_ACQUIRED@;own().stable=30;own().frame_min=@F9_MIN@;
 ++g_frame_id;touch_frame();
 pending("unrelated present does not finalize an incomplete game interval",own().own_est>0 && own().stable==30);
 // New real dump: 8ea3d22 lines 8683/9073/9233 have one fixed-root
 // candidate visible, hidden, visible with estimate -1, 2.63, -1. The
 // exact reset reason and object ownership were not recorded. Model the
 // previously observed empty global-epoch interruption with actual tracker
 // code; do not assume that was the trigger in this new legacy text dump.
 settings flicker_settings{};flicker_settings.hide_own=true;flicker_settings.own_distance=@FL_CAP@;
 program_info flicker_program{};flicker_program.dist_source="_WorldSpaceCameraPos";
 g_opengl_context=reinterpret_cast<void *>(0x5000);g_frame_id=1000;touch_frame();
 const bool b0=own_distance_excludes(flicker_settings,flicker_program,@FL_BODY@,own());
 const bool e0=own_distance_excludes(flicker_settings,flicker_program,@FL_CONTROL@,own());
 for(int i=0;i<30;++i){own().frame_min=@FL_EST@;++g_frame_id;touch_frame();}
 const bool b1=own_distance_excludes(flicker_settings,flicker_program,@FL_BODY@,own());
 const bool e1=own_distance_excludes(flicker_settings,flicker_program,@FL_CONTROL@,own());
 own().frame_min=1e9f;++g_frame_id;touch_frame();
 const bool b2=own_distance_excludes(flicker_settings,flicker_program,@FL_BODY@,own());
 const bool e2=own_distance_excludes(flicker_settings,flicker_program,@FL_CONTROL@,own());
 std::printf("Flicker tracker replay matches captured candidate decision pattern: %s\n",(!b0 && b1 && !b2) ? "yes (bug reproduced)" : "no (behavior changed)");
 // The far control is observed at 6742/6743 but not demonstrated to be an
 // enemy, and its third interval is synthetic. No nearby-enemy proof.
 assert(!e0 && !e1 && !e2);
 pending("flicker candidate consistently excluded through tracker interruption",b0 && b1 && b2);
 g_opengl_context=nullptr;own()=own_track{};pcache().count=0;icache().count=0;
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
 // Test actual lifecycle logger against the mock GL state. This does not test
 // hook dispatch or a Windows driver. The fixture's FBO 59 is not hardcoded.
 ints[GL_DRAW_FRAMEBUFFER_BINDING][0]=59;ints[GL_READ_FRAMEBUFFER_BINDING][0]=88;
 capture_gl_lifecycle("test_framebuffer_attachment",GL_FRAMEBUFFER,1703,GL_COLOR_ATTACHMENT0,2,3);
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
# Feed exact numeric values from preserved capture rows to the compiled tracker.
for token, value in {
 '@F9_EST@':events[45738]['own_est'], '@F9_BODY@':events[45738]['distance'],
 '@F9_PART@':events[45742]['distance'], '@F9_BEFORE@':events[68039]['before_estimate'],
 '@F9_AFTER@':events[68039]['estimate'], '@F9_RESET@':events[69239]['before_estimate'],
 '@F9_ACQUIRED@':events[264480]['before_estimate'], '@F9_MIN@':events[264480]['frame_min'],
 '@FL_CAP@':fl[9073]['owncap'], '@FL_BODY@':fl[9073]['dist'],
 '@FL_EST@':fl[9073]['ownest'], '@FL_CONTROL@':fl[10570]['dist'],
}.items(): head=head.replace(token,repr(value)+'f')
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
  lifecycle=[e for e in events if e['event']=='gl_lifecycle' and e.get('api')=='test_framebuffer_attachment']
  if len(draws)==3:
   assert len(lifecycle)==1 and lifecycle[0]['object']==1703 and lifecycle[0]['detail']==2 and lifecycle[0]['detail2']==3
   assert lifecycle[0]['draw_framebuffer']==59 and lifecycle[0]['read_framebuffer']==88 and lifecycle[0]['surface']!=0
  else: assert not lifecycle
  first=draws[0]; assert first['api'] in ('mock_draw','benchmark') and first['decision']=='budget' and first['count']==200
  assert first['program_epoch']==infos[0]['epoch'] and first['program']==infos[0]['program']
  assert first['color_mask']==[1,0,1,0] and len(first['depth_range'])==2
  assert 'scissor_box' in first and 'blend_src_alpha' in first and 'texture_2d' in first
  if first['api']=='benchmark': assert len(draws)==4000
  else: observed.append([x['decision'] for x in draws]); offsets.append((first['has_index_offset'],first['index_offset']))
 assert sorted(observed,key=len)==[['budget'],['budget','classifier_reject','direct']]
 assert sorted(offsets)==[(0,0),(1,40)] # client-memory addresses are never recorded
print('PASS: GL state round trips; tracker interrupted sequences; Strict/Wide cache mode switch; full inline code compiled against mock GL')
