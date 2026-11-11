#!/usr/bin/env python3
"""Compile actual intercepted sync wrapper bodies with a mock driver, not a GL driver."""
from pathlib import Path
import json, re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'source/opengl/opengl.cpp').read_text()
def function(name):
    m=re.search(r'(?:GLsync|GLenum|void) APIENTRY '+name+r'\([^\n]*\)\n\{',source)
    assert m, name
    i=m.end();depth=1
    while depth:
        if source[i]=='{':depth+=1
        elif source[i]=='}':depth-=1
        i+=1
    return source[m.start():i]
wrappers='\n'.join(function(n) for n in ('glFenceSync','glWaitSync','glClientWaitSync','glDeleteSync'))
head=r'''
#include <cassert>
#include <cstdint>
#include <filesystem>
#include "chams_capture.hpp"
#define APIENTRY
using GLenum=unsigned; using GLbitfield=unsigned; using GLuint64=uint64_t; using GLsync=void *;
thread_local void *g_opengl_context=nullptr;
thread_local uintptr_t active_context=0,active_surface=0;
namespace reshade::chams::capture { uintptr_t native_context(){return active_context;} uintptr_t native_surface(){return active_surface;} }
GLsync glFenceSync(GLenum,GLbitfield); void glWaitSync(GLsync,GLbitfield,GLuint64);
GLenum glClientWaitSync(GLsync,GLbitfield,GLuint64); void glDeleteSync(GLsync);
int fences=0,waits=0,clients=0,deletes=0; bool restart_during_wait=false; std::filesystem::path root;
GLsync driver_fence(GLenum condition,GLbitfield flags){assert(condition==7 && flags==0);++fences;return reinterpret_cast<GLsync>(0x12345678);}
void driver_wait(GLsync handle,GLbitfield flags,GLuint64 timeout){assert(handle==reinterpret_cast<GLsync>(0x12345678) && flags==0 && timeout==20);++waits;}
GLenum driver_client(GLsync handle,GLbitfield flags,GLuint64 timeout){assert(handle==reinterpret_cast<GLsync>(0x12345678) && flags==0 && timeout==20);++clients;
 if(restart_during_wait){reshade::chams::capture::runtime_stop();assert(reshade::chams::capture::runtime_start(root));}return 0x911a;}
void driver_delete(GLsync handle){assert(handle==reinterpret_cast<GLsync>(0x12345678));++deletes;}
namespace reshade::hooks {
 inline auto call(GLsync (*)(GLenum,GLbitfield)){return &driver_fence;}
 inline auto call(void (*)(GLsync,GLbitfield,GLuint64)){return &driver_wait;}
 inline auto call(GLenum (*)(GLsync,GLbitfield,GLuint64)){return &driver_client;}
 inline auto call(void (*)(GLsync)){return &driver_delete;}
}
'''
main=r'''
int main(int argc,char **argv){
 assert(argc==2);root=argv[1];active_context=3;active_surface=33;g_opengl_context=reinterpret_cast<void *>(3);
 auto off=glFenceSync(7,0);glWaitSync(off,0,20);assert(glClientWaitSync(off,0,20)==0x911a);glDeleteSync(off);
 assert(fences==1&&waits==1&&clients==1&&deletes==1);
 assert(reshade::chams::capture::runtime_start(root));
 auto f=glFenceSync(7,0);active_context=1;active_surface=11;g_opengl_context=reinterpret_cast<void *>(1);
 glWaitSync(f,0,20);assert(glClientWaitSync(f,0,20)==0x911a);glDeleteSync(f);
 // An intercepted wait begun in an old session must not emit its return into
 // a new session if Stop and Start occur during the driver call.
 restart_during_wait=true;assert(glClientWaitSync(f,0,20)==0x911a);restart_during_wait=false;
 reshade::chams::capture::runtime_stop();
 assert(fences==2&&waits==2&&clients==3&&deletes==2);
}
'''
with tempfile.TemporaryDirectory() as td:
    td=Path(td);p=td/'hooks.cpp';p.write_text(head+wrappers+main);exe=td/'hooks'
    subprocess.run(['g++','-std=c++17','-O0','-Wformat=2','-Werror=format','-I',str(root/'source'),str(p),str(root/'source/chams_capture.cpp'),'-pthread','-o',str(exe)],check=True)
    subprocess.run([str(exe),str(td)],check=True)
    sessions=sorted(td.glob('session-*'));assert len(sessions)==2
    events=[[json.loads(line) for file in sorted(session.glob('capture-*.jsonl')) for line in file.read_text().splitlines()] for session in sessions]
    sync=[[e for e in rows if e['event']=='gl_sync'] for rows in events]
    old=next(rows for rows in sync if rows);new=next(rows for rows in sync if not rows)
    assert [(e['api'],e['phase']) for e in old]==[('glFenceSync','return'),('glWaitSync','begin'),('glWaitSync','return'),('glClientWaitSync','begin'),('glClientWaitSync','return'),('glDeleteSync','return'),('glClientWaitSync','begin')]
    assert [e['sync_id'] for e in old]==[1,1,1,1,1,1,0]
    assert old[0]['context']!=old[1]['context'] and old[4]['result']==0x911a
    assert '305419896' not in json.dumps(old)
print('PASS: actual GL sync wrappers forward calls once and capture session-safe pseudonymous events (mock driver only)')
