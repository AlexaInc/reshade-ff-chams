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
capture11=json.loads((root/'tests/fixtures/capture-000011-selected.json').read_text())
assert capture11['archive_sha256']=='1943b7be1013e1a98b2b27fa78b72cd08c34e1a766d45c39cd15d9a3b8b48b2d'
c11={e['seq']:e for e in capture11['events']}
assert c11[41506]['event']=='present_begin' and c11[41506]['context']==1
assert c11[41507]['event']=='present' and c11[41507]['success']
assert c11[41513]['event']=='draw_begin' and c11[41513]['draw_id']==c11[41515]['draw_id']
assert c11[41514]['reason']=='no_observation' and c11[41514]['context']==3
assert c11[41514]['before_stable']==56 and c11[41514]['estimate']==-1
assert c11[41127]['decision']=='hide_own' and c11[41577]['decision']=='direct'
assert (c11[41127]['program'],c11[41127]['count'],c11[41127]['array_buffer'],c11[41127]['element_buffer'])==(c11[41577]['program'],c11[41577]['count'],c11[41577]['array_buffer'],c11[41577]['element_buffer'])
assert c11[41094]['decision']=='direct' and c11[41094]['stable']==c11[41127]['stable']
assert c11[1043359]['reason']=='out_of_range' and c11[1043359]['before_estimate']>0
assert [c11[n]['decision'] for n in (350394,364520,365803)]==['direct','hide_own','direct']
assert c11[365339]['world']==c11[365803]['world'] and c11[365339]['distance']==c11[365803]['distance']
assert c11[365757]['event']=='present_begin' and c11[365757]['context']==1
assert c11[365753]['legacy_global_frame']==3406 and c11[365759]['legacy_global_frame']==3407
assert c11[365759]['reason']=='no_observation' and c11[365759]['before_estimate']==c11[365753]['estimate']
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
 if(program==999 && std::strcmp(name,"_CharaLightIntensity")==0) return 8;          // F1 mock character
 if(program==999 && std::strcmp(name,"hlslcc_mtx4x4unity_ObjectToWorld")==0) return 1;
 if(program==999 && std::strcmp(name,"_WorldSpaceCameraPos")==0) return 9;
 if(program==777 && std::strcmp(name,"_ShadowThreshold")==0) return 11;
 if(program==778 && std::strcmp(name,"_OutLineWidth")==0) return 12;
 if(program==778 && std::strcmp(name,"_OutLineZOffset")==0) return 13;
 if(program==779 && std::strcmp(name,"_ShadeColor")==0) return 14;
 if(program==780 && std::strcmp(name,"_Shininess")==0) return 15;
 return -1;
}
float mock_world999[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,2.86f,1};   // translation drives distance
float mock_cam999[3]={0.01f,0.0f,0.0f};
void get_uniform(unsigned program, int loc, float *out){
 if(program==999 && loc==1){ std::memcpy(out,mock_world999,sizeof(mock_world999)); return; }
 if(program==999 && loc==9){ out[0]=mock_cam999[0]; out[1]=mock_cam999[1]; out[2]=mock_cam999[2]; for(int i=3;i<16;i++)out[i]=0.0f; return; }
 for(int i=0;i<16;++i)out[i]=float(i+1);
}
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
 // R1/Stage 4: a zero-observation frame is no information — estimate kept, hiding NOT suspended (no one-frame chams flash).
 update_own_estimate(); assert(own().own_est>0 && own().stable==30 && own().stale_frames==0 && own().no_info_streak==1);
 own().frame_min=2.17f; update_own_estimate(); assert(own().own_est>0 && own().stable==31 && own().stale_frames==0 && own().no_info_streak==0);
 // 31 consecutive zero-observation frames forget for good (spectate safety).
 own()=own_track{}; for(int i=0;i<30;i++){own().frame_min=2.17f;update_own_estimate();}
 for(int i=0;i<31;i++){update_own_estimate();}
 assert(own().own_est<0 && own().stable==0 && own().stale_frames==0 && own().no_info_streak==0);
 own()=own_track{}; for(int i=0;i<29;i++){own().frame_min=2.17f;update_own_estimate();}
 // A single sub-1.5m sample is the observed gun-switch/ADS bone-swing signature:
 // the update is skipped and the in-progress sequence (stable==29) survives.
 own().frame_min=1.0f; update_own_estimate(); assert(own().stable==29 && own().own_est<0 && own().cand>0);
 own().frame_min=2.17f;update_own_estimate();assert(own().stable==30 && own().own_est>0);
 for(int i=1;i<30;i++){own().frame_min=2.17f;update_own_estimate();}
 assert(own().own_est>0);own().frame_min=8.0f;update_own_estimate();assert(own().own_est>0 && own().stable>0 && own().stale_frames==1);
 pcache().count=0;g_settings.detect_mode=0;assert(!program_is_character(123));
 g_settings.detect_mode=1;assert(program_is_character(123));
 g_settings.detect_mode=0;assert(!program_is_character(123));
 // Cel/toon family (Stage 3 FIX B): classified in wide (1) and all (3) modes
 // exactly like the avatar tokens; strict (0) and skinned (2) unchanged.
 pcache().count=0;
 g_settings.detect_mode=1;
 assert(program_is_character(777));  // cel ramp body (_ShadowThreshold)
 assert(program_is_character(778));  // inverted-hull outline (_OutLineWidth/_OutLineZOffset)
 assert(program_is_character(779));  // MToon-style body (_ShadeColor)
 assert(!program_is_character(780)); // plain specular lit prop: no toon/avatar/bone tokens
 g_settings.detect_mode=0;
 assert(!program_is_character(777)); assert(!program_is_character(778)); assert(!program_is_character(779));
 g_settings.detect_mode=2;
 assert(!program_is_character(777)); assert(!program_is_character(778)); assert(!program_is_character(779));
 g_settings.detect_mode=3;
 assert(program_is_character(777)); assert(program_is_character(778)); assert(program_is_character(779)); assert(!program_is_character(780));
 g_settings.detect_mode=0;
 pcache().count=0;
 std::printf("cel_toon_family_detection: pass\n");
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
 std::printf("Flicker tracker replay matches captured candidate decision pattern: %s\n",(!b0 && b1 && b2) ? "no (Stage 4: empty frame retains exclusion — bug fixed)" : "yes (bug reproduced)");
 // The far control is observed at 6742/6743 but not demonstrated to be an
 // enemy, and its third interval is synthetic. No nearby-enemy proof.
 assert(!e0 && !e1 && !e2);
 pending("flicker candidate consistently excluded through tracker interruption (Stage 4: estimate retained, exclusion kept across the empty frame; cold-start draw still unexcluded)",b1 && b2);
 // Session 000011, seq 41094/41127: two temporally adjacent candidate
 // body/part draws have different decisions even with the same stable tracker.
 // The capture does not prove those draw origins share ownership.
 settings c11_settings{};c11_settings.hide_own=true;c11_settings.own_distance=@C11_CAP@;
 program_info c11_info{};c11_info.dist_source="_WorldSpaceCameraPos";
 own_track c11_state{};c11_state.own_est=@C11_DRAW_EST@;c11_state.stable=55;
 const bool c11_body=own_distance_excludes(c11_settings,c11_info,@C11_BODY@,c11_state);
 const bool c11_part=own_distance_excludes(c11_settings,c11_info,@C11_PART@,c11_state);
 assert(!c11_body && c11_part); // exactly observed, not proof of pixel ownership
 pending("capture11 putative body and part consistently excluded",c11_body && c11_part);
 // Real trace: unrelated present_begin/present (context 1, seq 41506/7),
 // then draw 11504 (context 3) invokes touch_frame; seq 41514 records the
 // precise no-observation transition. No replacement frame boundary is assumed.
 g_opengl_context=reinterpret_cast<void *>(0x6000);g_frame_id=2633;touch_frame();
 own().own_est=@C11_RESET_EST@;own().cand=@C11_RESET_CAND@;own().stable=56;own().frame_min=1e9f;
 const bool part_before=own_distance_excludes(c11_settings,c11_info,@C11_PART@,own());
 ++g_frame_id; touch_frame();
 const bool part_after=own_distance_excludes(c11_settings,c11_info,@C11_AFTER_PART@,own());
 // Stage 4: the unrelated-present frame carries no information; the estimate is kept and exclusion REMAINS ARMED (no one-frame chams flash).
 assert(part_before && part_after && own().own_est>0 && own().stable==56 && own().stale_frames==0);
 pending("capture11 unrelated present does not interrupt ownership (Stage 4: exclusion stays armed)",part_before && part_after);
 // Full captured body visible -> hidden -> visible, including the exact
 // unrelated present split within one observed clear-to-clear sequence.
 g_opengl_context=reinterpret_cast<void *>(0x6001);g_frame_id=3406;touch_frame();
 const bool body_initial=own_distance_excludes(c11_settings,c11_info,@C11_BODY_INITIAL@,own());
 own().own_est=@C11_BODY_ACQUIRED@;own().stable=30;
 const bool body_acquired=own_distance_excludes(c11_settings,c11_info,@C11_BODY_ACQ_DIST@,own());
 own().own_est=@C11_BODY_RESET@;own().cand=@C11_BODY_RESET@;own().stable=32;own().frame_min=1e9f;
 ++g_frame_id;touch_frame();
 const bool body_reset=own_distance_excludes(c11_settings,c11_info,@C11_BODY_RESET_DIST@,own());
 // Stage 4: the intervening empty frame carries no information; exclusion REMAINS ARMED (no one-frame chams flash).
 assert(!body_initial && body_acquired && body_reset);
 std::printf("capture11 exact body sequence reproduced (empty frame no longer suspends exclusion)\n");
 pending("capture11 body remains excluded across unrelated present (Stage 4: empty frame retains exclusion)",body_acquired && body_reset);
 // Synthetic same-distance control exposes the identity ambiguity. It is NOT
 // an observed nearby enemy; traces contain no enemy ownership labels.
 own_track equal_state{};equal_state.own_est=@C11_DRAW_EST@;equal_state.stable=55;
 const bool local_hypothesis=own_distance_excludes(c11_settings,c11_info,@C11_PART@,equal_state);
 const bool nearby_enemy_hypothesis=own_distance_excludes(c11_settings,c11_info,@C11_PART@,equal_state);
 pending("capture11 equal-distance nearby enemy remains visible",local_hypothesis && !nearby_enemy_hypothesis);
 g_opengl_context=nullptr;own()=own_track{};pcache().count=0;icache().count=0;
 // ===== Stage 3 FIX A: persistent displaced own cluster re-anchors =====
 // The own model drifted beyond the 0.3 m band in the field captures
 // (without-enemy ~2.44->2.78+; with-enemy ~2.25->2.95). The shifted modal
 // held for 31+ consecutive frames while R2 refused it; the estimate was
 // forgotten and re-acquired ~30 frames later (the visible flicker). A
 // walk-tolerant persistent modal (kDisplacedClusterFrames=12) now
 // re-anchors in place, stability kept, enemy untouched.
 g_opengl_context=reinterpret_cast<void *>(0x7300);g_frame_id=7000;touch_frame();
 own()=own_track{};forks()=own_fork_track{};
 settings dsp{};dsp.hide_own=true;dsp.own_distance=5.5f;
 program_info dsp_info{};dsp_info.dist_source="_WorldSpaceCameraPos";
 const float disp_root[9]={2.44f,2.44f,2.44f,2.44f,2.44f,2.44f,2.441f,2.44f,2.44f};
 const float disp_shifted[9]={2.75f,2.75f,2.75f,2.75f,2.75f,2.75f,2.751f,2.75f,2.75f};
 for(int i=0;i<31;i++){ for(float r : disp_root) note_character_distance(r); ++g_frame_id;touch_frame(); }
 assert(own().own_est>2.0f && own().stable==31);
 const float disp_est=own().own_est;
 for(int i=0;i<11;i++){
   for(float r : disp_shifted) note_character_distance(r);
   ++g_frame_id;touch_frame();
   assert(own().own_est==disp_est && own().stale_frames==i+1);             // streak builds, estimate kept
 }
 for(float r : disp_shifted) note_character_distance(r);
 ++g_frame_id;touch_frame();                                                // 12th consecutive persistent displaced frame
 assert(std::fabs(own().own_est-2.75f)<0.05f);                              // re-anchored to the own model's new distance
 assert(own().stable>=30 && own().stale_frames==0);                         // hiding stays armed, stability kept
 assert(own_distance_excludes(dsp,dsp_info,2.75f,own()));                   // own avatar hidden at the new band
 assert(!own_distance_excludes(dsp,dsp_info,2.44f,own()));                  // old band released, nothing else hidden
 assert(!own_distance_excludes(dsp,dsp_info,3.05f,own()));                  // far enemy untouched
 std::printf("sr_displaced_cluster_reanchor: pass\n");
 // After a jump, small per-frame movement keeps the tracking native: once the
 // re-anchor fires, est sits on the new cluster and the normal path follows
 // the walk (verified by the est staying near the walk position).
 own()=own_track{};forks()=own_fork_track{};
 for(int i=0;i<31;i++){ for(float r : disp_root) note_character_distance(r); ++g_frame_id;touch_frame(); }
 for(int i=0;i<12;i++){ for(float r : disp_shifted) note_character_distance(r); ++g_frame_id;touch_frame(); }
 float walk=2.75f;
 for(int i=0;i<10;i++){ walk+=0.05f; const float rr[9]={walk,walk,walk,walk,walk,walk,walk,walk,walk}; for(float r : rr) note_character_distance(r); ++g_frame_id;touch_frame(); }
 assert(std::fabs(own().own_est-walk)<0.30f);                               // normal path tracks the walk after re-anchoring
 std::printf("sr_displaced_cluster_walk_tracked: pass\n");
 // R2 preserved: an alternating modal (two distant clusters) never re-anchors.
 own()=own_track{};forks()=own_fork_track{};
 for(int i=0;i<31;i++){ for(float r : disp_root) note_character_distance(r); ++g_frame_id;touch_frame(); }
 const float r2_est=own().own_est;
 const float alt_a[9]={2.75f,2.75f,2.75f,2.75f,2.75f,2.75f,2.75f,2.75f,2.75f};
 const float alt_b[9]={1.90f,1.90f,1.90f,1.90f,1.90f,1.90f,1.90f,1.90f,1.90f};
 for(int i=0;i<40;i++){ for(float r : (i%2==0?alt_a:alt_b)) note_character_distance(r); ++g_frame_id;touch_frame(); }
 assert(own().own_est==r2_est || own().own_est<0);                          // never re-anchored; wipe at 31 as legacy (spectate safety)
 std::printf("sr_displaced_cluster_alternating_rejected: pass\n");
 // R2 preserved: a persistent FOREIGN modal re-anchors after 12 frames — the
 // documented, accepted trade (indistinguishable from a re-distanced own
 // model by distance alone; strictly sooner than the post-forget reacquire
 // the shipping code performs anyway).
 own()=own_track{};forks()=own_fork_track{};
 for(int i=0;i<31;i++){ for(float r : disp_root) note_character_distance(r); ++g_frame_id;touch_frame(); }
 for(int i=0;i<12;i++){ for(float r : disp_shifted) note_character_distance(r); ++g_frame_id;touch_frame(); }
 assert(std::fabs(own().own_est-2.75f)<0.05f);                              // persistent modal wins; documented trade
 std::printf("sr_displaced_cluster_persistent_modal_wins: pass\n");
 // Fix A1 (Stage 4, 2026-11-13): a pure zero-observation frame (no
 // comparable draws at all) must not break the displaced-cluster rebase
 // walk — same "no information, not contradictory evidence" principle the
 // Stage-4 stale-frames rule already applies to the same frame. A frame
 // that saw samples but none plausible (e.g. near-camera) still breaks it.
 g_opengl_context=reinterpret_cast<void *>(0x7200);g_frame_id=7500;touch_frame();
 own()=own_track{};forks()=own_fork_track{};
 settings a1{};a1.hide_own=true;a1.own_distance=5.5f;
 program_info a1_info{};a1_info.dist_source="_WorldSpaceCameraPos";
 const float a1_root[9]={2.44f,2.44f,2.44f,2.44f,2.44f,2.44f,2.441f,2.44f,2.44f};
 const float a1_disp[9]={2.75f,2.75f,2.75f,2.75f,2.75f,2.75f,2.751f,2.75f,2.75f};
 for(int i=0;i<31;i++){ for(float r : a1_root) note_character_distance(r); ++g_frame_id;touch_frame(); }
 const float a1_est=own().own_est;               // established reference ~2.44
 for(int i=0;i<5;i++){ for(float r : a1_disp) note_character_distance(r); ++g_frame_id;touch_frame(); }  // walk 1..5
 ++g_frame_id;touch_frame();                     // pure zero-observation frame: no draws at all
 for(int i=0;i<7;i++){ for(float r : a1_disp) note_character_distance(r); ++g_frame_id;touch_frame(); }   // walk 6..12
 assert(std::fabs(own().own_est-2.75f)<0.05f);   // re-anchored on the 12th qualifying displaced frame despite the zero-obs frame
 assert(own().stale_frames==0);                  // stability kept across the interruption
 assert(own_distance_excludes(a1,a1_info,2.75f,own()));   // hiding armed at the new band
 assert(!own_distance_excludes(a1,a1_info,2.44f,own()));    // old band released
 std::printf("sr_zero_observation_preserves_rebase_walk: pass\n");
 // An all-near-camera frame mid-walk still resets it (samples existed but
 // none plausible -> contradictory evidence, R2 semantics unchanged).
 own()=own_track{};forks()=own_fork_track{};
 for(int i=0;i<31;i++){ for(float r : a1_root) note_character_distance(r); ++g_frame_id;touch_frame(); }
 for(int i=0;i<5;i++){ for(float r : a1_disp) note_character_distance(r); ++g_frame_id;touch_frame(); }  // walk 1..5
 for(int i=0;i<9;i++) note_character_distance(1.2f);       // all samples implausible (<1.5 m)
 ++g_frame_id;touch_frame();                              // walk reset to 0 by fix-preserved semantics
 for(int i=0;i<7;i++){ for(float r : a1_disp) note_character_distance(r); ++g_frame_id;touch_frame(); }   // walk 1..7 only
 assert(own().own_est==a1_est);                           // never re-anchored (streak < 12)
 assert(own().stale_frames>0);                            // uncorroborated frames still suspend hiding
 assert(!own_distance_excludes(a1,a1_info,2.75f,own()));
 std::printf("sr_implausible_samples_still_reset_walk: pass\n");
 g_opengl_context=nullptr;own()=own_track{};forks()=own_fork_track{};pcache().count=0;icache().count=0;
 // 2026-11-12 packet (capture-d3) replay: the own model is drawn every frame
 // as a multipart root cluster at a near-constant distance (programs
 // 276/277/278/279 at 3.038 m, 8-9 draws), one animated bone origin
 // oscillating 2.58-2.96 m, one far-bone origin at 3.2 m, plus enemies far
 // outside (>=3.674 m observed). Before the multipart-modal fix the tracker
 // learned the bone and wiped out on the swing; now the root cluster is the
 // observation and a gun-switch/ADS swing (bone at 0.83 m) is survivable.
 g_opengl_context=reinterpret_cast<void *>(0x7000);g_frame_id=3000;touch_frame();
 own()=own_track{};
 settings d3_settings{};d3_settings.hide_own=true;d3_settings.own_distance=5.5f;
 program_info d3_info{};d3_info.dist_source="_WorldSpaceCameraPos";
 const float d3_roots[9]={3.037654f,3.037654f,3.037654f,3.037654f,3.037654f,3.037654f,3.037652f,3.037654f,3.037654f};
 for(int i=0;i<30;i++){
   for(float r : d3_roots) note_character_distance(r);
   note_character_distance(2.58f+0.02f*float(i%20));   // animated bone (frame minimum)
   note_character_distance(3.25f);                     // far bone
   note_character_distance(27.5f);                     // enemy, out of plausible range
   ++g_frame_id;touch_frame();
 }
 assert(own().stable==30);
 assert(std::fabs(own().own_est-3.038f)<0.05f);        // root cluster, not the bone
 assert(own_distance_excludes(d3_settings,d3_info,3.037654f,own()));   // root hidden
 assert(own_distance_excludes(d3_settings,d3_info,3.20f,own()));       // far bone in window
 assert(!own_distance_excludes(d3_settings,d3_info,3.674f,own()));     // nearest enemy untouched
 // Gun-switch / ADS transient: the bone swings to 0.83 m — no wipe, hiding persists.
 for(int i=0;i<15;i++){
   for(float r : d3_roots) note_character_distance(r);
   note_character_distance(0.83f);
   note_character_distance(3.25f);
   ++g_frame_id;touch_frame();
   assert(own().own_est>0 && own().stable>=30);
   assert(own_distance_excludes(d3_settings,d3_info,3.037654f,own()));
 }
 // Two enemy parts in range cannot outvote the 9-draw root cluster.
 for(int i=0;i<30;i++){
   for(float r : d3_roots) note_character_distance(r);
   note_character_distance(3.674f);note_character_distance(3.674f);
   ++g_frame_id;touch_frame();
 }
 assert(own().own_est>0 && own().stable>=75 && std::fabs(own().own_est-3.038f)<0.05f);
 assert(!own_distance_excludes(d3_settings,d3_info,3.674f,own()));
 // Stage 4: a brief zero-character frame keeps a valid estimate and exclusion armed.
 ++g_frame_id;touch_frame();
 assert(own().own_est>0 && own().stable>=75 && own().stale_frames==0);
 // ... while 31 consecutive zero-observation frames forget for good (death/spectate safety).
 for(int i=0;i<30;i++){++g_frame_id;touch_frame();}
 assert(own().own_est<0 && own().stable==0 && own().stale_frames==0);
 // Spread-out origins with no cluster learn nothing and never acquire.
 own()=own_track{};
 for(int i=0;i<30;i++){
   note_character_distance(2.0f);note_character_distance(3.0f);note_character_distance(4.0f);
   ++g_frame_id;touch_frame();
 }
 assert(own().own_est<0 && own().stable==0);
 // 2026-11-12 hardening: sparse frames with an established estimate.
 // Missing own-part draws (a weapon fork hidden for a frame, or culled)
 // must neither rebase the candidate to a lone enemy distance nor reset
 // stable. ADS/all-near frames must not destroy the estimate either.
 g_opengl_context=reinterpret_cast<void *>(0x7100);g_frame_id=4000;touch_frame();
 own()=own_track{};
 settings sg{};sg.hide_own=true;sg.own_distance=5.5f;
 program_info sg_info{};sg_info.dist_source="_WorldSpaceCameraPos";
 const float sg_roots[9]={3.037654f,3.037654f,3.037654f,3.037654f,3.037654f,3.037654f,3.037652f,3.037654f,3.037654f};
 for(int i=0;i<31;i++){
   for(float r : sg_roots) note_character_distance(r);
   note_character_distance(2.62f);note_character_distance(3.25f);   // bone + far bone
   ++g_frame_id;touch_frame();
 }
 assert(own().stable==31 && own().own_est>0);
 const float est_before=own().own_est;
 // Missing own forks, leaving a single enemy-only lone sample at 4.5 m: skipped, estimate kept.
 ++g_frame_id;note_character_distance(4.5f);touch_frame();
 assert(own().own_est==est_before && own().stable==31 && own().cand>0);
 // A lone plausible sample that does not corroborate the estimate (near enemy at 4.9 m): skipped.
 ++g_frame_id;note_character_distance(4.9f);touch_frame();
 assert(own().own_est==est_before && own().stable==31);
 // A lone plausible sample that DOES corroborate (a surviving root fork at 3.038 m): learned.
 ++g_frame_id;note_character_distance(3.038f);touch_frame();
 assert(own().stable==32 && own().own_est>0);
 // Two plausible samples, one corroborating the estimate, one enemy at 4.8 m: corroboration wins.
 ++g_frame_id;note_character_distance(3.04f);note_character_distance(4.8f);touch_frame();
 assert(own().stable==33 && own().own_est>0);
 // Nearby enemy preserved: the estimate stays glued to the root cluster; 3.674 m draw not excluded.
 assert(!own_distance_excludes(sg,sg_info,3.674f,own()));
 assert(own_distance_excludes(sg,sg_info,3.037654f,own()));
 // ADS/all-near burst immediately afterwards: estimate survives, hiding stays available.
 for(int i=0;i<6;i++){
   note_character_distance(1.13f);note_character_distance(1.01f);note_character_distance(1.37f);
   ++g_frame_id;touch_frame();
 }
 assert(std::fabs(own().own_est-est_before)<0.05f && own().stable>=33 && std::fabs(own().own_est-3.038f)<0.05f);
 // R1: exclusion is suspended during the zero-observation streak; a corroborated frame re-arms it.
 assert(own().stale_frames==6 && !own_distance_excludes(sg,sg_info,3.037654f,own()));
 for(float r : sg_roots) note_character_distance(r);
 ++g_frame_id;touch_frame();
 assert(own().stale_frames==0 && own_distance_excludes(sg,sg_info,3.037654f,own()));
 // Cold start: a sparse lone far sample still bootstraps the candidate (legacy behaviour kept).
 own()=own_track{};++g_frame_id;note_character_distance(4.6f);touch_frame();
 assert(own().cand>0 && own().stable==1 && own().own_est<0);
 // Weapon-switch gap (≥3 plausible but no cluster, e.g. roots missing for a frame): skipped, kept.
 own()=own_track{};
 for(int i=0;i<31;i++){ for(float r : sg_roots) note_character_distance(r); ++g_frame_id;touch_frame(); }
 const float ws_est=own().own_est;const int ws_stable=own().stable;
 ++g_frame_id;note_character_distance(2.62f);note_character_distance(3.20f);note_character_distance(3.30f);touch_frame();
 assert(own().own_est==ws_est && own().stable==ws_stable);
 // Estimate recovery after an honest zero-character wipe: re-acquires from the root cluster; enemy untouched.
 own()=own_track{};++g_frame_id;touch_frame();
 assert(own().own_est<0 && own().stable==0);
 for(int i=0;i<30;i++){ for(float r : sg_roots) note_character_distance(r); ++g_frame_id;touch_frame(); }
 assert(own().own_est>0 && own().stable>=30);
 assert(!own_distance_excludes(sg,sg_info,3.674f,own()));
 // ===== sr_* regression block (Stage 1: R1 stale grace, R2 modal corroboration, R3 cold-start modal anchoring) =====
 g_opengl_context=reinterpret_cast<void *>(0x7200);g_frame_id=5000;touch_frame();
 own()=own_track{};
 settings sr{};sr.hide_own=true;sr.own_distance=5.5f;
 program_info sr_info{};sr_info.dist_source="_WorldSpaceCameraPos";
 const float sr_roots[9]={3.037654f,3.037654f,3.037654f,3.037654f,3.037654f,3.037654f,3.037652f,3.037654f,3.037654f};
 // sr_r3_coldstart_modal_seed: with no established reference the multipart modal (>=3 votes) anchors the candidate.
 for(float r : {3.71f,3.71f,3.71f,2.10f,2.10f}) note_character_distance(r);
 ++g_frame_id;touch_frame();
 assert(own().cand>3.7f && own().cand<3.72f && own().stable==1 && own().stale_frames==0);
 std::printf("sr_r3_coldstart_modal_seed: pass\n");
 // sr_stale_grace_suspends_exclusion / sr_stale_rearm_next_corroborated_frame: acquire, suspend, re-arm.
 own()=own_track{};
 for(int i=0;i<31;i++){ for(float r : sr_roots) note_character_distance(r); ++g_frame_id;touch_frame(); }
 assert(own().own_est>0 && own().stable==31);
 const float sr_est=own().own_est;
 ++g_frame_id;touch_frame(); // zero-observation frame
 assert(own().own_est==sr_est && own().stable==31 && own().stale_frames==0 && own().no_info_streak==1);
 assert(own_distance_excludes(sr,sr_info,3.037654f,own()));    // Stage 4: hiding NOT suspended by a zero-observation frame
 std::printf("sr_zero_obs_keeps_exclusion_armed: pass\n");
 for(float r : sr_roots) note_character_distance(r);
 ++g_frame_id;touch_frame();
 assert(own().stale_frames==0 && own().stable==32 && own().no_info_streak==0);
 assert(own_distance_excludes(sr,sr_info,3.037654f,own()));    // still armed (isolation); corroborated frame relearns as before
 std::printf("sr_rearm_next_corroborated_frame: pass\n");
 // sr_stale_boundary_30_frames_retained / sr_stale_hard_wipe_at_31: hard-wipe boundary.
 own()=own_track{};
 for(int i=0;i<31;i++){ for(float r : sr_roots) note_character_distance(r); ++g_frame_id;touch_frame(); }
 const float sr_est2=own().own_est;
 for(int i=0;i<30;i++){++g_frame_id;touch_frame();}
 assert(own().own_est==sr_est2 && own().stale_frames==0 && own().no_info_streak==30);  // boundary: exactly 30 uncorroborated frames still retain
 std::printf("sr_boundary_30_frames_retained: pass\n");
 ++g_frame_id;touch_frame();                                    // 31st consecutive uncorroborated frame
 assert(own().own_est<0 && own().stable==0 && own().stale_frames==0 && own().no_info_streak==0);
 std::printf("sr_hard_wipe_at_31: pass\n");
 // sr_r2_foreign_modal_rejected: an established tracker refuses a foreign multi-vote majority (post-capture rebase bug).
 own()=own_track{};
 for(int i=0;i<31;i++){ for(float r : sr_roots) note_character_distance(r); ++g_frame_id;touch_frame(); }
 const float sr_est3=own().own_est;const int sr_stable3=own().stable;
 ++g_frame_id;
 for(int i=0;i<6;i++) note_character_distance(2.20f);  // enemy cluster, 6 votes, no own parts present
 note_character_distance(2.45f);                        // lone bone origin, no cluster
 touch_frame();
 assert(own().own_est==sr_est3 && own().stable==sr_stable3 && own().stale_frames==1);
 assert(!own_distance_excludes(sr,sr_info,3.037654f,own()));   // own draw must not disappear into the enemy modal
 assert(!own_distance_excludes(sr,sr_info,2.20f,own()));       // the foreign distance itself is never excluded (it is not the own model)
 std::printf("sr_r2_foreign_modal_rejected: pass\n");
 // sr_r2_persistent_foreign_modal_reanchors: Stage 3 FIX A supersedes the
 // legacy hard-forget for a *persistent* displaced modal (12 consecutive
 // walk-tolerant frames). A foreign camped cluster is indistinguishable
 // from a re-distanced own model by distance alone; the tracker re-anchors
 // — strictly sooner than the post-forget reacquire the legacy code
 // performed anyway (observed in both captures at the 31-frame forget).
 own()=own_track{};
 for(int i=0;i<31;i++){ for(float r : sr_roots) note_character_distance(r); ++g_frame_id;touch_frame(); }
 for(int i=0;i<12;i++){++g_frame_id; for(int k=0;k<6;k++) note_character_distance(2.20f); touch_frame();}
 assert(std::fabs(own().own_est-2.20f)<0.05f && own().stable>=30);
 std::printf("sr_r2_persistent_foreign_modal_reanchors: pass\n");
 // sr_r2_alternating_foreign_streak_hard_wipe: no persistence (two distant
 // clusters alternating) -> no re-anchor; the estimate is forgotten at the
 // kStaleFrameLimit boundary as before (death/spectate safety).
 own()=own_track{};
 for(int i=0;i<31;i++){ for(float r : sr_roots) note_character_distance(r); ++g_frame_id;touch_frame(); }
 for(int i=0;i<31;i++){++g_frame_id; for(int k=0;k<6;k++) note_character_distance(i%2==0?2.20f:4.30f); touch_frame();}
 assert(own().own_est<0 && own().stable==0);
 std::printf("sr_r2_alternating_foreign_streak_hard_wipe: pass\n");
 // sr_r2_near_reference_cluster_learned: a cluster inside the 0.3m band is still accepted (not over-gated).
 own()=own_track{};
 for(int i=0;i<31;i++){ for(float r : sr_roots) note_character_distance(r); ++g_frame_id;touch_frame(); }
 for(float r : sr_roots) note_character_distance(3.30f);  // modal 0.26m away from the reference (<0.3m)
 ++g_frame_id;touch_frame();
 assert(own().stale_frames==0 && own().stable==32);
 std::printf("sr_r2_near_reference_cluster_learned: pass\n");
 g_opengl_context=nullptr;own()=own_track{};pcache().count=0;icache().count=0;
 // ===== sr_zero_obs_* block (Stage 4: zero-observation frames are no information —
 //       the brief low-frequency own-avatar chams flash fix. Reproduces the exact
 //       failure pattern measured in the 2026-11-13 captures: an isolated frame with
 //       zero comparable draws used to suspend hiding for the following frame,
 //       flipping the whole avatar (root and fork) into the chams passes for one
 //       frame; both appear as a sporadic full-body flash.) =====
 {
   g_opengl_context=reinterpret_cast<void *>(0x7400);g_frame_id=6000;touch_frame();
   own()=own_track{};
   settings zo{};zo.hide_own=true;zo.own_distance=5.5f;
   program_info zo_info{};zo_info.dist_source="_WorldSpaceCameraPos";
   const float zo_roots[9]={3.037654f,3.037654f,3.037654f,3.037654f,3.037654f,3.037654f,3.037652f,3.037654f,3.037654f};
   for(int i=0;i<31;i++){ for(float r : zo_roots) note_character_distance(r); ++g_frame_id;touch_frame(); }
   assert(own().own_est>0 && own().stable==31);
   const float zo_est=own().own_est;
   // sr_zero_obs_keeps_exclusion_armed_next_frame:
   // one isolated zero-observation frame keeps the estimate, and the very next frame's exclusion IS ARMED (the old code suspended it).
   ++g_frame_id;touch_frame();
   assert(own().own_est==zo_est && own().stable==31 && own().stale_frames==0 && own().no_info_streak==1);
   assert(own_distance_excludes(zo,zo_info,3.037654f,own()));
   for(float r : zo_roots) note_character_distance(r); ++g_frame_id;touch_frame();
   assert(own().own_est==zo_est && own().stable==32 && own().no_info_streak==0);
   std::printf("sr_zero_obs_keeps_exclusion_armed_next_frame: pass\n");
   // sr_zero_obs_fork_eligible: F1 stays eligible across a zero-observation frame (binding not disturbed, no absence accrues).
   ++g_frame_id;touch_frame();
   assert(own_fork_eligible(zo,own()));
   std::printf("sr_zero_obs_fork_eligible: pass\n");
   for(float r : zo_roots) note_character_distance(r); ++g_frame_id;touch_frame();  // corroborated frame: backstop cleared
   assert(own().no_info_streak==0);
   // sr_zero_obs_30_frames_retained / sr_zero_obs_31st_forgets_backstop:
   // the combined uncorroborated backstop preserves the legacy forget boundary (death/spectate safety).
   for(int i=0;i<30;i++){++g_frame_id;touch_frame();}
   assert(own().own_est==zo_est && own().stale_frames==0 && own().no_info_streak==30);
   ++g_frame_id;touch_frame();                                               // 31st consecutive uncorroborated frame
   assert(own().own_est<0 && own().stable==0 && own().no_info_streak==0);
   // re-acquire; verify alternating contradiction+no-information forgets at the same combined boundary
   own()=own_track{};
   for(int i=0;i<31;i++){ for(float r : zo_roots) note_character_distance(r); ++g_frame_id;touch_frame(); }
   for(int i=0;i<15;i++){ ++g_frame_id; for(int k=0;k<6;k++) note_character_distance(i%2==0?2.20f:4.30f); touch_frame();  // foreign modal (alternating) -> skip, stale
                          ++g_frame_id; touch_frame(); }                                              // zero-observation frame
   assert(own().own_est>0.0f && own().no_info_streak==30 && own().stale_frames==15);   // 15 skips + 15 zero-obs: retained one frame short of the boundary
   ++g_frame_id;touch_frame();                                               // 31st uncorroborated frame
   assert(own().own_est<0 && own().stable==0);
   std::printf("sr_zero_obs_mixed_backstop_forgets_at_31: pass\n");
   // sr_contradictory_skip_still_suspends: frames with contradictory evidence keep R1 semantics (hiding suspended).
   own()=own_track{};
   for(int i=0;i<31;i++){ for(float r : zo_roots) note_character_distance(r); ++g_frame_id;touch_frame(); }
   ++g_frame_id; for(int k=0;k<6;k++) note_character_distance(2.20f); touch_frame();   // single frame foreign modal (no streak yet) -> skip
   assert(own().stale_frames==1 && !own_distance_excludes(zo,zo_info,3.037654f,own()));
   std::printf("sr_contradictory_skip_still_suspends: pass\n");
   g_opengl_context=nullptr;own()=own_track{};
 }
 // ===== sr_fork_* block (Stage 2 F1: own-fork fingerprint binding) =====
 // The own attachment fork is the accessory draw with a dedicated fingerprint
 // seen inside the root draw span. These tests pin F1's binding, rejection,
 // stale-state, enemy-interleaving, buffer-reuse, absence/unbinding and reset
 // semantics against the mock GL.
 g_opengl_context=reinterpret_cast<void *>(0x80000);g_frame_id=6000;touch_frame();
 own()=own_track{};pcache().count=0;icache().count=0;forks()=own_fork_track{};
 g_settings=settings{};g_settings.hide_own=true;g_settings.own_distance=4.9f;g_settings.detect_mode=1;g_settings.render_mode=1;g_settings.enabled=true;
 settings &srf = g_settings;
 program_info srf_p{};srf_p.dist_source="_WorldSpaceCameraPos";
 own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;   // armed tracker (as in a live game)
 auto srf_note=[&](float dist, unsigned array, unsigned element, unsigned prog, int fcount, unsigned tex, uint64_t off){
   ints[GL_ARRAY_BUFFER_BINDING][0]=array;
   ints[GL_ELEMENT_ARRAY_BUFFER_BINDING][0]=element;
   at(GL_TEXTURE0);bt(GL_TEXTURE_2D,tex);
   note_character_distance(dist);   // as draw_chams does before the F1 hook
   return own_fork_note_draw(srf,srf_p,dist,own(),(int)prog,fcount,off);
 };
 auto srf_frame=[&](){ ++g_frame_id; touch_frame(); };
 auto srf_root_span=[&](){ for(int k=0;k<9;k++) srf_note(2.86f,1639,712,282,10491,656,(uint64_t)k*4700+4000); };
 auto srf_fork=[&](float dist){ return srf_note(dist,1335,1336,284,864,628,0); };
 // --- sr_fork_bind_after_30_eligible_frames: 29 frames of bracketed observation do not bind; the 30th does.
 srf_frame(); own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
 for(int i=0;i<29;i++){
   for(int k=0;k<4;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
   assert(!srf_fork(1.90f));
   for(int k=4;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
   srf_frame();
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
 }
 {   // the 30th corroborated, bracketed frame binds the fork mid-boundary
   for(int k=0;k<4;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
   srf_fork(1.90f);
   for(int k=4;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
   bool bound=false; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].bound) bound=true;
   assert(!bound);          // binding happens at the boundary finalise, not mid-frame
   srf_frame();
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   bool bound2=false; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].bound) bound2=true;
   assert(bound2);
 }
 std::printf("sr_fork_bind_after_30_eligible_frames: pass\n");
 // --- sr_fork_bound_fork_hides_in_span: once bound, the fork hides when the span began this frame.
 for(int k=0;k<4;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
 assert(srf_fork(1.70f));
 for(int k=4;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
 srf_frame();
 own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
 std::printf("sr_fork_bound_fork_hides_in_span: pass\n");
 // --- sr_fork_before_span_never_hidden: bound fork drawn before any root this frame stays visible.
 {
   assert(!srf_fork(1.70f));   // first character draw of the frame: span has not begun
   for(int k=0;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
   srf_frame();
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
 }
 std::printf("sr_fork_before_span_never_hidden: pass\n");
 // --- sr_fork_stale_frame_inert: on an uncorroborated frame F1 does nothing (R1 own-avatar visibility).
 {
   // Lone fork sample: the frame cannot corroborate -> tracker skip -> stale=1, estimate retained (R1).
   assert(!srf_note(1.90f,1335,1336,284,864,628,0));   // span empty this frame: not hidden regardless
   srf_frame();
   assert(own().stale_frames==1);                      // R1: estimate retained, hiding suspended
   assert(!own_fork_note_draw(srf,srf_p,1.90f,own(),284,864,0));   // stale frame: F1 inert, fork visible
   srf_frame();                                        // nothing observed -> combined backstop=2; suspension streak kept at 1 (Stage 4)
   assert(own().stale_frames==1 && own().no_info_streak==2);
   // A corroborated frame re-arms hiding on the very next frame.
   own().stale_frames=0;
   for(int k=0;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
   assert(srf_fork(1.90f));
   srf_frame();
   own().stale_frames=0;
 }
 std::printf("sr_fork_stale_frame_inert: pass\n");
 // --- sr_fork_enemy_interleaved_out_of_band_never_binds: bracketed but beyond the 1.5 m learning sanity -> never bound.
 {
   g_opengl_context=reinterpret_cast<void *>(0x80001);g_frame_id=7000;touch_frame();
   own()=own_track{};forks()=own_fork_track{};
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   for(int i=0;i<40;i++){
     srf_root_span();
     srf_note(4.60f,933,979,197,17439,607,0);      // an enemy drawn inside the own span, 1.74 m away: out of sanity band
     srf_fork(1.90f);
     srf_frame();
     own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   }
   bool enemy_bound=false; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].bound){ int fp_prog=forks().entries[i].fp.program; int fp_arr=forks().entries[i].fp.array_buffer; if(fp_prog==197&&fp_arr==933) enemy_bound=true; }
   assert(!enemy_bound);
 }
 std::printf("sr_fork_enemy_interleaved_out_of_band_never_binds: pass\n");
 // --- sr_fork_tail_enemy_never_binds: an enemy drawn after the own span every frame never enters binding.
 {
   g_opengl_context=reinterpret_cast<void *>(0x80002);g_frame_id=8000;touch_frame();
   own()=own_track{};forks()=own_fork_track{};
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   for(int i=0;i<40;i++){
     srf_root_span();
     srf_note(3.70f,933,979,197,17439,607,0);      // sane gap (0.84 m) but never bracketed
     srf_frame();
     own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   }
   bool enemy_used=false; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].fp.array_buffer==933) enemy_used=true;
   assert(!enemy_used);   // never even tracked: not observed inside the span
 }
 std::printf("sr_fork_tail_enemy_never_binds: pass\n");
 // --- sr_fork_inband_enemy_is_root_not_fork: a bracketed draw at est distance is a root member, never a fork candidate.
 {
   g_opengl_context=reinterpret_cast<void *>(0x80003);g_frame_id=9000;touch_frame();
   own()=own_track{};forks()=own_fork_track{};
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   for(int i=0;i<40;i++){
     srf_root_span();
     srf_note(2.88f,933,979,197,17439,607,0);      // inside est±0.2: indistinguishable from a root pass
     srf_frame();
     own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   }
   bool enemy_used=false; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].fp.array_buffer==933) enemy_used=true;
   assert(!enemy_used);   // root member: hidden by the distance predicate, not tracked as a fork
   assert(own_distance_excludes(srf,srf_p,2.88f,own()));
 }
 std::printf("sr_fork_inband_enemy_is_root_not_fork: pass\n");
 // --- sr_fork_absence_unbinds_after_90_corroborated_frames: a bound fork unseen for 91 corroborated character-active frames is forgotten.
 {
   g_opengl_context=reinterpret_cast<void *>(0x80004);g_frame_id=10000;touch_frame();
   own()=own_track{};forks()=own_fork_track{};
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   for(int i=0;i<30;i++){ for(int k=0;k<4;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000); assert(!srf_fork(1.90f)); for(int k=4;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000); srf_frame(); }
   bool bound=false; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].bound) bound=true;
   assert(bound);              // bound at the 30th corroborated boundary
   for(int i=0;i<90;i++){ srf_root_span(); srf_frame(); }
   bool still=false; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].bound) still=true;
   assert(still);              // exactly 90 absences: still bound (boundary is exclusive)
   srf_root_span(); srf_frame();
   bool gone=true; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].bound) gone=false;
   assert(gone);               // 91st corroborated absence: forgotten (outfit swap / id reuse)
 }
 std::printf("sr_fork_absence_unbinds_after_90_corroborated_frames: pass\n");
 // --- sr_fork_bound_fork_drawing_stays_bound: a bound fork hidden every frame is never mistaken for absent.
 {
   g_opengl_context=reinterpret_cast<void *>(0x8000d);g_frame_id=18000;touch_frame();
   own()=own_track{};forks()=own_fork_track{};
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   for(int i=0;i<30;i++){ for(int k=0;k<4;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000); assert(!srf_fork(1.90f)); for(int k=4;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000); srf_frame(); }
   bool bound=false; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].bound) bound=true;
   assert(bound);
   for(int i=0;i<120;i++){
     for(int k=0;k<4;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
     assert(srf_fork(1.90f));   // bound and hidden every frame
     for(int k=4;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
     srf_frame();
   }
   bool still=false; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].bound) still=true;
   assert(still);               // hidden-by-F1 counts as seen: no self-unbind
   int streak=0; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].bound) streak=forks().entries[i].absent_streak;
   assert(streak==0);
 }
 std::printf("sr_fork_bound_fork_drawing_stays_bound: pass\n");
 // --- sr_fork_same_tuple_different_texture_no_hide: same GL fingerprint shape but different texture is a different fingerprint.
 {
   g_opengl_context=reinterpret_cast<void *>(0x80005);g_frame_id=11000;touch_frame();
   own()=own_track{};forks()=own_fork_track{};
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   for(int i=0;i<30;i++){ for(int k=0;k<4;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000); assert(!srf_fork(1.90f)); for(int k=4;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000); srf_frame(); own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0; }
   srf_frame();
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   for(int k=0;k<4;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
   assert(!srf_note(1.90f,1335,1336,284,864,999,0));   // same VBO/EBO/program/count, different texture: NOT the bound fork
   for(int k=4;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
   assert(srf_fork(1.90f));                            // the real fork is still hidden
   srf_frame();
 }
 std::printf("sr_fork_same_tuple_different_texture_no_hide: pass\n");
 // --- sr_fork_buffer_reuse_count_change_no_hide: same buffer ids, different geometry (count changed) is a different fingerprint.
 {
   g_opengl_context=reinterpret_cast<void *>(0x80006);g_frame_id=12000;touch_frame();
   own()=own_track{};forks()=own_fork_track{};
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   for(int i=0;i<30;i++){ for(int k=0;k<4;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000); assert(!srf_fork(1.90f)); for(int k=4;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000); srf_frame(); own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0; }
   srf_frame();
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   for(int k=0;k<4;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
   assert(!srf_note(1.90f,1335,1336,284,500,628,0));   // count changed: not the bound fork (id reuse deflected)
   for(int k=4;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
   assert(srf_fork(1.90f));
   srf_frame();
 }
 std::printf("sr_fork_buffer_reuse_count_change_no_hide: pass\n");
 // --- sr_fork_context_reset_clears: a device/native-context reset forgets fingerprints.
 {
   g_opengl_context=reinterpret_cast<void *>(0x80007);g_frame_id=13000;touch_frame();
   own()=own_track{};forks()=own_fork_track{};
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   for(int i=0;i<30;i++){ for(int k=0;k<4;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000); assert(!srf_fork(1.90f)); for(int k=4;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000); srf_frame(); }
   bool bound=false; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].bound) bound=true;
   assert(bound);
   g_opengl_context=reinterpret_cast<void *>(0x80008);touch_frame();   // context reset
   bool any=false; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used) any=true;
   assert(!any);
   assert(own().own_est<0 && own().stable==0);
 }
 std::printf("sr_fork_context_reset_clears: pass\n");
 // --- sr_fork_hard_wipe_clears: crossing kStaleFrameLimit forgets fingerprints with the estimate.
 {
   g_opengl_context=reinterpret_cast<void *>(0x80009);g_frame_id=14000;touch_frame();
   own()=own_track{};forks()=own_fork_track{};
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   for(int i=0;i<30;i++){ for(int k=0;k<4;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000); assert(!srf_fork(1.90f)); for(int k=4;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000); srf_frame(); }
   bool bound=false; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].bound) bound=true;
   assert(bound);
   for(int i=0;i<31;i++){ ++g_frame_id; touch_frame(); }   // 31 zero-observation frames -> hard wipe
   assert(own().own_est<0 && own().stable==0 && own().stale_frames==0);
   bool any=false; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used) any=true;
   assert(!any);
 }
 std::printf("sr_fork_hard_wipe_clears: pass\n");
 // --- sr_fork_out_of_sanity_resets_streak: a bracketed frame beyond the 1.5 m sanity band breaks consecutiveness.
 {
   g_opengl_context=reinterpret_cast<void *>(0x8000a);g_frame_id=15000;touch_frame();
   own()=own_track{};forks()=own_fork_track{};
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   for(int i=0;i<29;i++){ srf_root_span(); srf_fork(1.90f); srf_frame(); own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0; }
   srf_root_span(); srf_fork(4.90f); srf_frame();   // fork swing beyond sanity: streak breaks
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   srf_root_span(); srf_fork(1.90f); assert(!own_fork_should_hide(srf,1.90f,own(),forks(),own_fork_fp{284,1335,1336,864,628,0})); srf_frame();
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   bool bound=false; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].bound) bound=true;
   assert(!bound);   // one out-of-band frame reset the 30-frame consecutiveness
   srf_frame();
 }
 std::printf("sr_fork_out_of_sanity_resets_streak: pass\n");
 // --- sr_fork_obs_buffer_full_resets_streak: a tracked candidate whose observation is dropped
 // because the observation buffer is full cannot keep its in-run streak (it must not bind from it).
 {
   g_opengl_context=reinterpret_cast<void *>(0x8000e);g_frame_id=19000;touch_frame();
   own()=own_track{};forks()=own_fork_track{};
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   for(int i=0;i<29;i++){
     for(int k=0;k<4;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
     assert(!srf_fork(1.90f));
     for(int k=4;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
     srf_frame();
   }
   int streak=-1; for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].fp.array_buffer==1335) streak=forks().entries[i].in_run_streak;
   assert(streak==29);   // sanity: the fork is tracked with a nearly-complete streak
   // Overflow frame: 16 distinct fingerprints (far out of band, never bracketed, but still
   // recorded into the observation buffer) fill it BEFORE the fork is noted. They allocate
   // nothing at the boundary, so the fork track survives intact.
   for(int i=0;i<16;i++) srf_note(8.0f,3200+i,3300+i,900+i,5000+i,700+i,(uint64_t)i*131+17);
   for(int k=0;k<4;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
   assert(!srf_fork(1.90f));   // drawn bracketed, but the observation buffer is full: dropped
   for(int k=4;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
   srf_frame();
   int streak2=-2, absent2=-2, seen2=-2;
   bool found=false;
   for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].fp.array_buffer==1335) { streak2=forks().entries[i].in_run_streak; absent2=forks().entries[i].absent_streak; seen2=forks().entries[i].seen_this_frame; found=true; }
   assert(found);              // the track survives (nothing allocated over it)
   assert(streak2==0);         // the dropped observation broke the streak (regression guard)
   assert(absent2==0);         // it counted as seen: no absence accrued
   assert(seen2==0);           // seen flag consumed by the boundary
   // And one more valid bracketed frame cannot complete a binding from the old streak.
   for(int k=0;k<4;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
   assert(!srf_fork(1.90f));
   for(int k=4;k<9;k++) srf_note(2.86f,1639,712,282,10491,650,(uint64_t)k*4700+4000);
   srf_frame();
   int streak3=-2; bool bound3=false;
   for(int i=0;i<kForkMaxEntries;i++) if(forks().entries[i].used&&forks().entries[i].fp.array_buffer==1335) { streak3=forks().entries[i].in_run_streak; bound3=forks().entries[i].bound!=0; }
   assert(streak3==1);
   assert(!bound3);
 }
 std::printf("sr_fork_obs_buffer_full_resets_streak: pass\n");
 // --- sr_fork_coldstart_never_binds: without an established estimate a bracketed fork never binds (cold start / spectate).
 {
   g_opengl_context=reinterpret_cast<void *>(0x8000b);g_frame_id=16000;touch_frame();
   own()=own_track{};forks()=own_fork_track{};
   for(int i=0;i<40;i++){
     srf_note(0.80f,1335,1336,284,864,628,0);          // near-camera transient: skipped
     srf_note(2.0f,1639,712,282,10491,650,1000);       // spread origins: no cluster, never learns
     srf_note(3.0f,1639,712,282,10491,650,2000);
     srf_note(4.0f,1639,712,282,10491,650,3000);
     srf_frame();
   }
   assert(own().own_est<0 && own().stable==0);         // tracker never armed
   for(int i=0;i<kForkMaxEntries;i++) assert(forks().entries[i].used==0);   // F1 never engaged
 }
 std::printf("sr_fork_coldstart_never_binds: pass\n");
 // --- sr_fork_end_to_end_binding_and_hide: full draw path; the fork starts hidden only after 30 corroborated in-span frames.
 {
   g_opengl_context=reinterpret_cast<void *>(0x8000c);g_frame_id=17000;touch_frame();
   own()=own_track{};pcache().count=0;icache().count=0;forks()=own_fork_track{};
   g_settings=settings{};g_settings.hide_own=true;g_settings.own_distance=4.9f;g_settings.detect_mode=1;g_settings.render_mode=1;g_settings.enabled=true;
   own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
   int fork_hidden=0, fork_total=0, root_hidden=0;
   for(int f=0;f<40;f++){
     srf_frame();
     own().own_est=2.86f;own().cand=2.86f;own().stable=1000;own().stale_frames=0;
     // nine root passes (buf 1639), interleaved with two fork passes (buf 1335)
     for(int span=0;span<2;span++){
       for(int k=0;k<4;k++){ const float w[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,2.86f,1}; std::memcpy(mock_world999,w,sizeof(w)); ints[GL_ARRAY_BUFFER_BINDING][0]=1639; ints[GL_ELEMENT_ARRAY_BUFFER_BINDING][0]=712; at(GL_TEXTURE0);bt(GL_TEXTURE_2D,650); up(999);
         capture_gl_scope rscope("root", GL_TRIANGLES, 10491); draw_chams(GL_TRIANGLES,10491,[&]{ }, &rscope, 4000+(span*4+k)*4700); if(std::strcmp(rscope.decision,"hide_own")==0)++root_hidden; }
       { const float w[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,1.90f,1}; std::memcpy(mock_world999,w,sizeof(w)); ints[GL_ARRAY_BUFFER_BINDING][0]=1335; ints[GL_ELEMENT_ARRAY_BUFFER_BINDING][0]=1336; at(GL_TEXTURE0);bt(GL_TEXTURE_2D,628); up(999);
         capture_gl_scope fscope("fork", GL_TRIANGLES, 864); draw_chams(GL_TRIANGLES,864,[&]{ }, &fscope, 0); ++fork_total; if(std::strcmp(fscope.decision,"hide_own")==0)++fork_hidden; }
     }
     { const float w[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,2.86f,1}; std::memcpy(mock_world999,w,sizeof(w)); ints[GL_ARRAY_BUFFER_BINDING][0]=1639; ints[GL_ELEMENT_ARRAY_BUFFER_BINDING][0]=712; at(GL_TEXTURE0);bt(GL_TEXTURE_2D,650); up(999);
       capture_gl_scope rscope("root", GL_TRIANGLES, 10491); draw_chams(GL_TRIANGLES,10491,[&]{ }, &rscope, 400000); if(std::strcmp(rscope.decision,"hide_own")==0)++root_hidden; }
   }
   // Root passes are hidden on every frame (armed tracker, in band).
   assert(root_hidden==40*9);
   // The fork is bound after 30 corroborated in-span frames, then hidden.
   assert(fork_total==80);
   assert(fork_hidden==2*(40-30));
   std::printf("sr_fork_end_to_end_binding_and_hide: pass (%d/%d fork draws hidden after binding)\n", fork_hidden, fork_total);
 }
 g_opengl_context=nullptr;own()=own_track{};pcache().count=0;icache().count=0;forks()=own_fork_track{};
 // ===== end sr_* block =====
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
 '@C11_CAP@':c11[41127]['own_cap'], '@C11_DRAW_EST@':c11[41127]['own_est'],
 '@C11_BODY@':c11[41094]['distance'], '@C11_PART@':c11[41127]['distance'],
 '@C11_RESET_EST@':c11[41514]['before_estimate'], '@C11_RESET_CAND@':c11[41514]['before_candidate'],
 '@C11_AFTER_PART@':c11[41577]['distance'],
 '@C11_BODY_INITIAL@':c11[350394]['distance'], '@C11_BODY_ACQUIRED@':c11[364473]['estimate'],
 '@C11_BODY_ACQ_DIST@':c11[364520]['distance'], '@C11_BODY_RESET@':c11[365759]['before_estimate'],
 '@C11_BODY_RESET_DIST@':c11[365803]['distance'],
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
