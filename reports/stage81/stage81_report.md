# Stage 81 — Genuine Mock Failure Tests

**Status: TEST-ONLY. No production source changes. Mock PASS. No commit/push. STOP.**
**Baseline:** Stage 79 implementation, HEAD `038fa1a`. **Production files unchanged in Stage 81** (only `tests/run_mock_gl_tests.py` modified).

## Executive Summary

| Item | Value |
|---|---|
| **Mock failure controls** | 5 configurable (GetError, FBO status, shader/program/gen failure) |
| **Genuine tests added** | 7 new (4, 4b, 4c, 4d, 4e, 4f, 4g) + Test 8 fixed (genuine) |
| **Existing tests** | **PASS** (single/multi-ctx, frame mismatch, Direct regression) |
| **All tests pass** | **YES** (mock PASS, off 149.7 ns/draw, on 304228.9 ns/draw) |
| **Production changes** | **NONE** (`chams_gl.inl` 0 lines, `chams.hpp`/`chams_overlay.inl` Stage 79 only) |

---

## 1. Genuine Failure Tests Added

All tests exercise **actual production functions** (`overlay_build`, `overlay_capture_mesh`, `draw_chams`, `render_overlay`). **No manual `has_mask=false`**, **no manual counter increments**. Each test resets mock state.

| Test | Injected failure | Observed state | Result |
|---|---|---|---|
| **4** GL error capture | `mock_get_error_value = GL_NO_ERROR+1` (ctx 10, frame 500) | `has_mask=false`, `captured=invalid`, `comp_gl_error`+1 | **PASS** |
| **4b** Incomplete FBO | `mock_fbo_status = COMPLETE+1` after size init (ctx 12, frame 502) | `has_mask=false`, `captured=invalid`, `comp_fbo_incomplete`+1 | **PASS** |
| **4c** Shader failure | `mock_fail_shader=true` (ctx 13) | `overlay_build()==false`, `ready=false`, `tried=true` (no false ready) | **PASS** |
| **4d** Program failure | `mock_fail_program=true` (ctx 14) | `overlay_build()==false`, `ready=false`, `tried=true` | **PASS** |
| **4e** Failed capture → Direct fallback | `mock_get_error_value=GL_NO_ERROR+1` (ctx 15, frame 504, program 456) | `has_mask=false`, `scope.decision=="direct_overlay_fallback"` (Direct executed) | **PASS** |
| **4f** Reset → valid capture | Reset to `NO_ERROR`/`COMPLETE` (ctx 15, frame 505) | `has_mask=true`, `captured==505` | **PASS** |
| **4g** Gen failure | `mock_fail_gen=true` (ctx 16) | `built==true`, `ready==true`, `fbo==0` (**limitation**: production does not validate Gen* results) | **PASS** (documents gap) |
| **8** Composite no_mask (fixed) | Genuine failed capture (inject error, capture, reset, composite) | `has_mask=false` (genuine), `comp_no_mask`+1 | **PASS** |

**Exact injected conditions:**
- GL error: `mock_get_error_value = (GL_NO_ERROR + 1)` (any non-NO_ERROR triggers `err != NO_ERROR` in production).
- Incomplete: `mock_fbo_status = (GL_FRAMEBUFFER_COMPLETE + 1)` (any non-COMPLETE triggers `status != COMPLETE`).
- Shader: `mock_fail_shader=true` → `CreateShader` returns 0, `GetShaderiv` sets `ok=0`.
- Program: `mock_fail_program=true` → `CreateProgram` returns 0, `GetProgramiv` sets `linked=0`.
- Gen: `mock_fail_gen=true` → `Gen*` sets `*p=0`.

**Assertions:** All via production return values + `ov()` state + `comp_*` counters (incremented by production, not test). No manual `has_mask` sets (except Test 8 previously manual, now genuine via failed capture).

## 2. Mock Failure Controls (5)

Added to `tests/run_mock_gl_tests.py` head (C++ mock):
1. `mock_get_error_value` (unsigned, init `GL_NO_ERROR`) — controls `get_error()`.
2. `mock_fbo_status` (unsigned, init `GL_FRAMEBUFFER_COMPLETE`) — controls `check_fbo_status()`.
3. `mock_fail_shader` (bool) — controls `create_shader()`/`get_shaderiv()`.
4. `mock_fail_program` (bool) — controls `create_program()`/`get_programiv()`.
5. `mock_fail_gen` (bool) — controls `gen_framebuffers`/`gen_vertex_arrays`/`gen_buffers`/`gen_textures`.

Each test sets control → exercises production → asserts → resets control. Fresh contexts (10,12,13,14,15,16) avoid polluting ctx 1/2.

## 3. Existing Regression Results

**Command:** `PATH=/tmp/fakebin:$PATH python3 tests/run_mock_gl_tests.py` (VM GCC 9.4 + shim).

**Result:** **PASS** — `PASS: GL state round trips; tracker interrupted sequences; Strict/Wide cache mode switch; full inline code compiled against mock GL`. Benchmark: off 149.7 ns/draw, on 304228.9 ns/draw.

**Existing Stage 79 tests (preserved):**
- Test 1 Single-ctx capture+composite — **PASS**
- Test 2 Independent no invalid reuse — **PASS**
- Test 3 Shared local FBO/VAO — **PASS**
- Test 5 Frame mismatch — **PASS**
- Test 6 Context switch — **PASS**
- Test 7 Direct regression — **PASS**

**All tests pass: YES.**

## 4. Limitations

- **Gen failure not detected by production:** `overlay_build` does not validate `Gen*` results (sets `ready=true` even with `fbo==0`). Test 4g documents this. **Production gap, not fixed** (test-only stage). Recommend future production validation of `Gen*` results.
- **Concurrent access not simulated:** Tests single-threaded, sequential switches. No threads. (WGL thread-affine assumed, per Stage 80.)
- **Stale HGLRC reuse not simulated:** No `wglDeleteContext` mock. (Stage 80 MEDIUM risk remains.)
- **Windows runtime not tested:** Mock only. No DLL build claims. No BlueStacks/FF/MAX.

---

*Test-only. No production source changes. Mock PASS. STOP. Awaiting approval.*
