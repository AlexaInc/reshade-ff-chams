# Stage 82 — Validate OpenGL Object Creation

**Status: IMPLEMENTED + MOCK-TESTED. No commit/push. No Windows build. STOP.**
**Baseline:** HEAD `038fa1a`, branch `agent/chams-investigation-build`. Stage 79/81 uncommitted. **Direct untouched** (`chams_gl.inl` 0 lines).

## Executive Summary

| Item | Value |
|---|---|
| **Fix** | Validate FBO/VAO/texture/VBO IDs in `overlay_build()` before `ready=true` |
| **Behavior** | Zero ID → `ready=false`, no capture/composite, `comp_init_fail`+1, partial zeroed |
| **Shader/program** | Reviewed: compile/link already validated; added `comp_init_fail` + explicit `program==0` check |
| **Deletion** | **None** (avoid unsafe cleanup; partial leaks, driver reclaims) |
| **Per-context** | Preserved (map unchanged) |
| **Mock** | **PASS** (6 new gen tests + existing, off 186.6 ns/draw, on 405435.0 ns/draw) |
| **Windows build** | **NOT verified** (no MSVC, no CI) |
| **Runtime** | **NOT verified** (no BlueStacks/FF) |

---

## 1. Exact Source Changes

**File:** `source/chams_overlay.inl` only (production). `source/chams_gl.inl` **unchanged**.

| Location | Before | After |
|---|---|---|
| Shader fail (`:vs/fs`) | `return false` (no counter) | `comp_init_fail`+1, `return false` |
| Program create | No zero check (via link fail) | Explicit `if (program==0) { comp_init_fail+1; return false; }` |
| Program link fail | `return false` (no counter) | `comp_init_fail`+1, `return false` |
| Gen* validation (after `GenFramebuffers`) | `ready=true` unconditionally | `if (fbo==0\|\|vao==0\|\|mask_tex==0\|\|vbo==0) { comp_init_fail+1; zero all; return false; }` then `ready=true` |

**No deletion** of partial objects (avoid unsafe cleanup when ownership uncertain; leak acceptable, driver reclaims on ctx delete). Partial IDs zeroed so cannot be mistaken for valid.

## 2. Object Creation Validation

- **Framebuffer:** `gl.GenFramebuffers(1, &g.fbo)` → check `g.fbo != 0`.
- **VAO:** `gl.GenVertexArrays(1, &g.vao)` → check `g.vao != 0`.
- **Texture:** `gl.GenTextures(1, &g.mask_tex)` → check `g.mask_tex != 0`.
- **Buffer:** `gl.GenBuffers(1, &g.vbo)` → check `g.vbo != 0`.
- **Program:** `gl.CreateProgram()` → check `g.program != 0` (explicit, before Attach). Link status already checked.
- **Shader:** `compile()` already checks `vs/fs != 0` (via `GetShaderiv` status). Added counter.

All failures: `ready=false` (initial false, never set true), `tried=true` (prevents retry, preserves no-stall policy), `comp_init_fail`+1, return false. Capture (`overlay_capture_mesh` checks `!ready` → return) and composite (`render_overlay` checks `!ready` → `comp_init_fail` + skip) **cannot proceed**. Partial zeroed.

## 3. Partial Initialization Behavior

If any Gen* fails (e.g., VAO 0, others succeed):
- `comp_init_fail`+1, all IDs zeroed (`fbo=vao=mask_tex=vbo=0`), `ready=false`, return false.
- Partial objects (succeeded Gen* IDs) **leaked** (no deletion, per task to avoid unsafe cleanup). Driver reclaims on ctx delete. Acceptable (few objects, few ctxs).
- `tried=true` prevents retry on same ctx (no-stall). New ctx (fresh entry, `tried=false`) can succeed (clean retry via new context, tested).

## 4. Genuine Test Results (Mock)

**Command:** `PATH=/tmp/fakebin:$PATH python3 tests/run_mock_gl_tests.py`. **PASS**, off 186.6 ns/draw, on 405435.0 ns/draw.

**Mock controls added:** `mock_fail_fbo`, `mock_fail_vao`, `mock_fail_tex`, `mock_fail_vbo` (per-object, OR with master `mock_fail_gen`). Each test uses fresh ctx, sets control, exercises `overlay_build()`, asserts, resets.

| Test | Injected | Observed | Result |
|---|---|---|---|
| **82-1** FBO gen fail (ctx 20) | `mock_fail_fbo=true` | `build==false`, `ready==false`, `tried==true`, all IDs 0, `comp_init_fail`+1 | **PASS** |
| **82-2** VAO gen fail (ctx 21) | `mock_fail_vao=true` | `build==false`, `ready==false`, all 0, `comp_init_fail`+1 | **PASS** |
| **82-3** Texture gen fail (ctx 22) | `mock_fail_tex=true` | `build==false`, `ready==false`, `mask_tex==0`, `comp_init_fail`+1 | **PASS** |
| **82-4** Buffer gen fail (ctx 23) | `mock_fail_vbo=true` | `build==false`, `ready==false`, `vbo==0`, `comp_init_fail`+1 | **PASS** |
| **82-5** Partial init (only VAO fails) | `mock_fail_vao=true` (ctx 24) | `build==false`, `ready==false`, all 0 (partial zeroed) | **PASS** |
| **82-6** Clean retry | Same ctx 24 retry → `false` (no-retry); fresh ctx 25 → `true`, `ready==true` | **PASS** (policy: same-ctx no-retry, new-ctx succeeds) |
| **Existing** shader/program (4c/4d), GL error (4), incomplete (4b), fallback (4e), reset (4f), multi-ctx, frame mismatch, Direct | — | **PASS** (all preserved) |
| **4g updated** (was bug doc) | `mock_fail_gen=true` | Now `built==false`, `ready==false` (fix verified, was `built==true` bug) | **PASS** |

All tests exercise **actual production functions** (`overlay_build`). No manual `has_mask`, no manual counters (counters incremented by production).

## 5. Direct Mode Diff Verification

- `git diff source/chams_gl.inl` → **0 lines** (verified). Direct rendering logic **untouched**.
- `git diff --stat`: 3 files (chams.hpp +2, chams_overlay.inl +136/−, run_mock_gl_tests.py +243/−). No Direct changes.

## 6. Remaining Risks

- **Partial leak:** Failed build leaks succeeded Gen* objects (no deletion). Driver reclaims on ctx delete. Acceptable.
- **No retry:** Same-ctx failure never retries (`tried`). New ctx succeeds. Documented policy (no-stall).
- **Windows build NOT verified** (no MSVC/CI). Mock PASS does not prove Windows PASS.
- **Runtime NOT verified** (no BlueStacks/FF). Standalone v8 remains last Windows evidence.

## 7. Build/Runtime Verification Status

- **Mock-tested:** **PASS** (genuine, 6 new + existing).
- **Windows build-verified:** **NO** (not built, requires CI, no commit/push).
- **Windows standalone-tested:** **NO** (v8 last, production fix not Windows-tested).
- **Runtime-verified:** **NO** (BlueStacks/FF/MAX/AeonSR none tested).

---

*Minimal production fix + mock regression tests. No commit/push. STOP. Awaiting approval.*
