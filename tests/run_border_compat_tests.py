#!/usr/bin/env python3
"""Exercise the actual Direct draw_chams helpers against the repository's fake GL.
This is a state/control-flow test, not a driver or pixel test.
"""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
script = (root / 'tests/run_mock_gl_tests.py').read_text()
start = script.index('int main(int argc, char **argv) {')
pre = script[:start]
pre = pre.replace('std::atomic<unsigned> g_frame_id{1};',
                  'std::atomic<unsigned> g_frame_id{1}; std::atomic<unsigned> g_radar_bodies{0};')
main = r'''
struct Call { int depth_func, depth_write, poly, depth_test; int r,g,b,a; };
int main() {
 using namespace reshade::chams;
 g_opengl_context=reinterpret_cast<void *>(1);
 mock_native_context=1;
 vp(0,0,800,600); en(GL_DEPTH_TEST); dm(1); cm(1,1,1,1); df(GL_LEQUAL); dr(0.0f,1.0f); pm(GL_FRONT_AND_BACK,GL_FILL);
 bfs(GL_ONE,GL_ZERO,GL_ONE,GL_ZERO); bes(GL_FUNC_ADD,GL_FUNC_ADD); up(456);
 auto invoke = [&](int fill_mode,bool glow,bool wire,bool compat,bool through,bool open) {
  g_settings=settings{}; g_settings.enabled=true;g_settings.render_mode=1;g_settings.detect_mode=0;
  g_settings.hide_own=false;g_settings.through_walls=through;g_settings.border_compat=compat;
  g_settings.glow=glow;g_settings.glow_mode=fill_mode;g_settings.glow_alpha=0.7f;g_settings.glow_open=open;
  g_settings.wire=wire;g_settings.wire_alpha=0.7f;g_settings.wire_open=open;g_settings.stroke=false;
  g_budget_left=110;g_overlay_open=false; ++g_frame_id;previous_render_mode()=1;
  en(GL_DEPTH_TEST);dm(1);cm(1,1,1,1);df(GL_LEQUAL);dr(0,1);pm(GL_FRONT_AND_BACK,GL_FILL);up(456);
  auto before=save_state(); std::vector<Call> calls;
  draw_chams(GL_TRIANGLES,200,[&]{ calls.push_back({ints[GL_DEPTH_FUNC][0],ints[GL_DEPTH_WRITEMASK][0],ints[GL_POLYGON_MODE][0],enabled[GL_DEPTH_TEST],ints[GL_COLOR_WRITEMASK][0],ints[GL_COLOR_WRITEMASK][1],ints[GL_COLOR_WRITEMASK][2],ints[GL_COLOR_WRITEMASK][3]}); });
  auto after=save_state();
  assert(before.depth_test==after.depth_test && before.depth_mask==after.depth_mask && before.depth_func==after.depth_func);
  assert(before.depth_range[0]==after.depth_range[0] && before.depth_range[1]==after.depth_range[1]);
  assert(before.polygon_mode[0]==after.polygon_mode[0] && before.line_width==after.line_width);
  assert(before.blend==after.blend && before.cull_face==after.cull_face && before.blend_src_rgb==after.blend_src_rgb);
  for(int i=0;i<4;i++)assert(before.color_mask[i]==after.color_mask[i] && before.blend_color[i]==after.blend_color[i]);
  std::vector<Call> paint; for(auto c:calls) if(!c.depth_write && c.r&&c.g&&c.b&&c.a)paint.push_back(c);
  return paint;
 };
 for(int mode=0;mode<4;mode++) for(bool compat:{false,true}) for(bool through:{false,true}) for(bool open:{false,true}) {
  auto p=invoke(mode,true,false,compat,through,open);
  int hidden=(mode==settings::glow_cloth_tint)?2:1;
  int opened=(mode==settings::glow_cloth_only || !open)?0:1;
  assert(int(p.size())==hidden+opened);
  for(int i=0;i<hidden;i++) {assert(p[i].depth_test && p[i].depth_func==((compat&&through)?GL_ALWAYS:GL_GREATER));assert(p[i].poly==GL_FILL);}
  if(opened) assert(p.back().depth_func==GL_LEQUAL && p.back().poly==GL_FILL);
 }
 for(bool compat:{false,true}) for(bool through:{false,true}) for(bool open:{false,true}) {
  auto p=invoke(0,false,true,compat,through,open); assert(p.size()==(open?2u:1u));
  assert(p[0].depth_test && p[0].depth_func==((compat&&through)?GL_ALWAYS:GL_GREATER) && p[0].poly==GL_LINE);
  if(open)assert(p[1].depth_func==GL_LEQUAL && p[1].poly==GL_LINE);
 }
 auto p=invoke(0,true,true,true,true,true);assert(p.size()==4);assert(p[0].depth_func==GL_ALWAYS && p[1].depth_func==GL_LEQUAL && p[2].depth_func==GL_ALWAYS && p[3].depth_func==GL_LEQUAL);
 // Existing claim then restore means a later normal depth-tested wall can pass.
 float pixel=.8f;
 auto model=[&]{if(enabled[GL_DEPTH_TEST] && ints[GL_DEPTH_WRITEMASK][0]) {float n=floats[GL_DEPTH_RANGE][0],f=floats[GL_DEPTH_RANGE][1];pixel=n+.8f*(f-n);}};
 claim_depth(0,0.0f,GL_ALWAYS,model);assert(pixel==0.0f);restore_depth(model);assert(pixel>.79f && .4f<pixel);
 std::puts("PASS: border-compatible Direct mode actual GL call/state regressions; late wall limitation modeled");
}
'''
# The upstream mock script builds its generated C++ into `head`; run its setup
# only, then append our focused main, avoiding unrelated known tracker failures.
namespace = {'__file__': str(root / 'tests/run_mock_gl_tests.py')}
exec(compile(pre + "\n'''\n", 'mock-setup', 'exec'), namespace)
source = namespace['head'] + main
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'mock.cpp';p.write_text(source); exe=Path(d)/('test.exe' if __import__('os').name=='nt' else 'test')
 subprocess.run(['g++','-std=c++17','-O0','-Wformat=2','-Werror=format',str(p),str(root/'source/chams_capture.cpp'),'-pthread','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
