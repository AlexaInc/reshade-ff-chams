#!/usr/bin/env python3
"""Synthetic provider tests only; no Windows driver or emulator validation."""
import pathlib, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
    exe = pathlib.Path(d) / 'host-caller-probe-test'
    subprocess.run(['g++','-std=c++17','-O2','-pthread','-I',str(root/'tests/fake_windows'),str(root/'tests/chams_host_caller_probe_test.cpp'),'-o',str(exe)], check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
print('PASS: self-activation without env var, sampling limits, tracking gates, draw IDs, session reuse and concurrency (synthetic host only)')
