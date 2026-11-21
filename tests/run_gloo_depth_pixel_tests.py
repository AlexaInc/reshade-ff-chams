#!/usr/bin/env python3
"""Mesa/EGL single-pixel regression for depth ordering, not a BlueStacks test."""
import ctypes as C, os
os.environ.setdefault('EGL_PLATFORM','surfaceless')
egl=C.CDLL('libEGL.so.1'); gl=C.CDLL('libGL.so.1')
def fn(lib,name,restype,args):
 f=getattr(lib,name); f.restype=restype;f.argtypes=args; return f
vp=C.c_void_p; I=C.c_int; F=C.c_float; D=C.c_double; U=C.c_uint
getdisplay=fn(egl,'eglGetDisplay',vp,[vp]);initialize=fn(egl,'eglInitialize',U,[vp,C.POINTER(I),C.POINTER(I)])
bind=fn(egl,'eglBindAPI',U,[U]);choose=fn(egl,'eglChooseConfig',U,[vp,C.POINTER(I),C.POINTER(vp),I,C.POINTER(I)])
create_surface=fn(egl,'eglCreatePbufferSurface',vp,[vp,vp,C.POINTER(I)]);create_context=fn(egl,'eglCreateContext',vp,[vp,vp,vp,C.POINTER(I)])
makecurrent=fn(egl,'eglMakeCurrent',U,[vp,vp,vp,vp]);geterror=fn(egl,'eglGetError',U,[])
dpy=getdisplay(None); a=I();b=I(); assert dpy and initialize(dpy,C.byref(a),C.byref(b)),hex(geterror());assert bind(0x30A2)
attrs=(I*19)(0x3033,1,0x3040,8,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3025,24,0x3038,0,0,0)
cfg=vp();num=I();assert choose(dpy,attrs,C.byref(cfg),1,C.byref(num)) and num.value
surf=create_surface(dpy,cfg,(I*5)(0x3057,8,0x3056,8,0x3038));ctx=create_context(dpy,cfg,None,(I*1)(0x3038));assert surf and ctx and makecurrent(dpy,surf,surf,ctx),hex(geterror())
void=None
viewport=fn(gl,'glViewport',void,[I,I,I,I]);clearcolor=fn(gl,'glClearColor',void,[F,F,F,F]);clear=fn(gl,'glClear',void,[U]);enable=fn(gl,'glEnable',void,[U]);disable=fn(gl,'glDisable',void,[U]);depthfunc=fn(gl,'glDepthFunc',void,[U]);depthmask=fn(gl,'glDepthMask',void,[C.c_ubyte]);colormask=fn(gl,'glColorMask',void,[C.c_ubyte]*4);depthrange=fn(gl,'glDepthRange',void,[D,D]);color=fn(gl,'glColor3f',void,[F,F,F]);begin=fn(gl,'glBegin',void,[U]);vertex=fn(gl,'glVertex3f',void,[F,F,F]);end=fn(gl,'glEnd',void,[]);read=fn(gl,'glReadPixels',void,[I,I,I,I,U,U,vp]);error=fn(gl,'glGetError',U,[])
viewport(0,0,8,8)
def quad(z,rgb):
 color(*rgb);begin(5)
 for x,y in [(-1,-1),(1,-1),(-1,1),(1,1)]:vertex(x,y,z)
 end()
def reset():
 enable(0x0B71);depthmask(1);colormask(1,1,1,1);depthrange(0,1);depthfunc(0x0203);clearcolor(0,0,0,1);clear(0x4000|0x0100)
def pixel():
 out=(C.c_ubyte*4)();read(4,4,1,1,0x1908,0x1401,out);return tuple(out[:3])
def scene(seed,wall_ndc=-0.2,wall_depth_test=True,part=False):
 reset();quad(0.6,(0,1,0)) # green character at normalized depth .8
 if seed:
  colormask(0,0,0,0);depthrange(1,0);depthfunc(0x0203);quad(0.6,(0,1,0));depthrange(0,1);colormask(1,1,1,1)
 if not wall_depth_test:disable(0x0B71)
 quad(wall_ndc,(1,0,0)) # red wall at depth .4, unless changed
 if part:quad(-0.4,(0,0,1)) # later part at depth .3
 p=pixel(); assert error()==0;return p
assert scene(False)==(255,0,0), 'late wall must cover unseeded character'
assert scene(True)==(0,255,0), 'reversed seed must block depth-tested wall at .4'
assert scene(True,wall_ndc=-0.8)==(255,0,0), 'nearer wall at .1 still wins'
assert scene(True,wall_depth_test=False)==(255,0,0), 'depth-disabled wall still wins'
assert scene(True,part=True)==(0,255,0), 'depth seed can also block a later character part'
print('PASS: Mesa EGL pixels: reversed seed blocks modeled late wall; closer/depth-disabled walls and later-part risks confirmed')
