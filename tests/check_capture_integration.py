#!/usr/bin/env python3
"""Static integration sanity checks; not a GL/driver coverage proof."""
import pathlib,re
r=pathlib.Path(__file__).resolve().parents[1]
gl=(r/'source/opengl/opengl.cpp').read_text();menu=(r/'source/chams_menu.cpp').read_text();wgl=(r/'source/opengl/opengl_hooks_wgl.cpp').read_text();proj=(r/'ReShade.vcxproj').read_text();overlay=(r/'source/runtime_gui.cpp').read_text()
pattern=r'(?:extern "C" )?void APIENTRY (gl(?:Multi)?Draw\w+)\(([^\n]*)\)\n\{\s*reshade::chams::capture_gl_scope chams_capture_scope\("([^"]+)"'
hooks=re.findall(pattern,gl)
assert len(hooks)==18 and all(name==label for name,args,label in hooks)
assert gl.count('&chams_capture_scope')==6
assert 'source\\chams_capture.cpp' in proj and 'source\\chams_capture.hpp' in proj
for required in ('runtime_created','runtime_destroyed','runtime_switched','runtime_shared','runtime_present_begin','runtime_present'):
 assert required in wgl
for required in ('Start tracking##capture','Stop tracking##capture','Add marker##capture','capture_status.dropped','capture_status.io_error'):
 assert required in menu
assert 'CHAMS TRACKING ACTIVE' in overlay
print('PASS: static integration coverage for 18 intercepted draw hooks, six decision wrappers, WGL events, UI and project')
