#!/usr/bin/env python3
import os, pathlib, re, select, subprocess, tempfile
with tempfile.TemporaryDirectory() as d:
 env=dict(os.environ, ARENA_GUEST_MARKER_TEST_DIR=d)
 exe=pathlib.Path(d)/'marker-test'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-DMARKER_GUEST_TEST','source/guest_marker.cpp','-o',str(exe)],check=True)
 session='testmarker001'
 p=subprocess.Popen([str(exe),session],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1,env=env)
 def line():
  assert select.select([p.stdout],[],[],5)[0], 'guest test timed out'
  return p.stdout.readline().strip()
 try:
  first=line(); assert 'state=ready' in first,first
  (pathlib.Path(d)/f'arena-marker-{session}.rotate').touch()
  second=line(); assert 'state=rotated' in second,second
  addr=lambda x: re.search(r'guest_va=(0x[0-9a-f]+)',x).group(1)
  marker=lambda x: re.search(r'marker=([0-9a-f]{128})$',x).group(1)
  assert addr(first)==addr(second) and marker(first)!=marker(second)
  (pathlib.Path(d)/f'arena-marker-{session}.stop').touch()
  last=line();assert 'state=freed' in last and 'munmap_ok=1' in last,last
  assert p.wait(timeout=5)==0
 finally:
  if p.poll() is None: p.kill();p.wait()
print('PASS: controlled guest marker starts, rotates at same VA, and unmaps')
