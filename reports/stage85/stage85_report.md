# Stage 85 — HGLRC / Thread Identity Diagnostics (DIAGNOSTIC-ONLY)

**Status: DIAGNOSTIC-ONLY. No rendering behavior changes. No commit/push. STOP.**

Baseline: branch `agent/chams-investigation-build`, HEAD `2d38ef7b710528ae118b72296698b6331c500dcb`, Stage 84 `reports/stage84/stage84_runtime_analysis.md`. User evidence `ctx_mismatch=1653 success=0` preserved.

## 1. Objective

Determine whether Overlay capture and composite use different HGLRC values or different threads. No rendering fix, no DLL deploy, no protected-game testing.

## 2. Production changes

### Files changed

- `source/chams_overlay.inl` only (72 lines: 67 insertions, 5 deletions, final 616 lines). `source/chams_gl.inl` 0 lines — Direct unchanged.

### Exact locations

**a) `overlay_gpu` struct (`:68`)** — added `bool diag_capture_logged = false` (once-per-context rate limit for capture-ok log). No rendering impact.

**b) Reason counters (`:124-128` after `comp_gl_error`)**
```
comp_disabled_no_context, comp_disabled_render_mode, comp_disabled_not_enabled,
comp_disabled_menu_open, comp_disabled_no_effect
```
`comp_disabled` kept as total (compat). All `std::atomic<unsigned>` relaxed.

**c) Diagnostic helpers (`:84-102`, after `current_context_key()`)**
```
diag_thread_id(): GetCurrentThreadId() on Windows, 0 on mock
diag_hdc(): wglGetCurrentDC() on Windows, nullptr on mock
```
Query-only, no GL calls, no context switches, no locks.

**d) Build success (`:328-330`)** — extended log: `thread=%lu key=%p hdc=%p`.

**e) Capture success (`:432-437` inside `overlay_capture_mesh`)** — once-per-context log: `[CHAMS] overlay capture ok thread=%lu key=%p hdc=%p frame=%u` (guarded by `diag_capture_logged`).

**f) Periodic summary (`:459-476`, every 300 frames)** — extended composite line:
```
[CHAMS] overlay composite: attempts=... disabled=%u [no_context=%u render_mode=%u not_enabled=%u menu_open=%u no_effect=%u] ... thread=%lu key=%p hdc=%p found=%d owner=%p
```
Adds reason breakdown + identity (`diag_thread_id`, `current_context_key`, `diag_hdc`) + `ov_find()` result (`found`, `owner`). Brief lock via `ov_find()` every 300 frames only (no per-draw log).

**g) Disabled split (`:478-508`)** — from `if (A||B||C||D) return` to 4 ordered early returns (priority = source order: no_context > render_mode > not_enabled > menu_open):
```
if (g_opengl_context==nullptr) -> no_context
if (render_mode!=0)           -> render_mode
if (!enabled)                 -> not_enabled
if (g_overlay_open)           -> menu_open
```
Same behavior, total preserved. Late `!glow && !stroke` now also increments `no_effect`.

### What was NOT changed

- No `ov_find()` → `ov()`.
- No FBO/VAO/texture/program ownership.
- No `render_overlay()` GL calls, no context switches, no locks across rendering ops (only `ov_find()` lock every 300 frames in summary).
- No `begin_frame`/`poll_hotkey`/`export_mask` changes.
- `opengl_hooks_wgl.cpp` swap hook (`:1331`) not modified — HDC passed via `wglGetCurrentDC()` query, not parameter plumbing (avoids hook signature change).

## 3. Example log format

Build (once per context):
```
[CHAMS] overlay renderer ready (mask framebuffer + composite shader) thread=1234 key=00000000A1B2C3D4 hdc=0000000000120034
```

Capture (once per context):
```
[CHAMS] overlay capture ok thread=1234 key=00000000A1B2C3D4 hdc=0000000000120034 frame=8505
```

Composite (every 300 frames, `g_frame_id % 300 == 0`):
```
[CHAMS] overlay composite: attempts=902 success=0 ctx_mismatch=150 no_mask=0 fbo_incomplete=0 init_fail=3 disabled=749 [no_context=10 render_mode=700 not_enabled=5 menu_open=20 no_effect=14] frame_mismatch=0 gl_error=0 thread=5678 key=00000000DEADBEEF hdc=0000000000120034 found=0 owner=0000000000000000
```

Interpretation: same `key` at capture and composite → same HGLRC; different `key` → cross-context (H1). Different `thread` → thread mismatch (H2 in Stage 84: capture on draw thread vs composite on swap thread). `found=0` with `key` present confirms no entry for that context (expected `ctx_mismatch` path).

## 4. Mock test results

**Command:** `PATH=/tmp/fakebin:$PATH python3 tests/run_mock_gl_tests.py` (shim for `std::chars_format` on GCC 9.4.0).

**Result: PASS** — `MOCK BENCHMARK: off 170.1 ns/draw, on 274048.4 ns/draw` + `PASS: GL state round trips; ...` (Stage 82 precondition re-established for ctx 24 after map clear; fix applied in `tests/run_mock_gl_tests.py`).

**New Stage 85 tests (8, inside `tests/run_mock_gl_tests.py` Stage 82 block, +103 lines):**

| Test | Condition | Counter | Verified |
|------|-----------|---------|----------|
| 85-1 | `g_opengl_context==nullptr` | `no_context` | total+reason =1, others 0 |
| 85-2 | `render_mode=1` | `render_mode` | |
| 85-3 | `enabled=false` | `not_enabled` | |
| 85-4 | `g_overlay_open=true` | `menu_open` | |
| 85-5 | `glow=false stroke=false` (late) | `no_effect` | with valid capture/frame |
| 85-6 | `nullptr` + `render_mode=1` + `!enabled` + `menu_open` | priority: `no_context` wins | others 0 |
| 85-7 | 4 sequential disables | `disabled==4`, sum(reasons)==disabled | |
| 85-8 | `diag_*` helpers + capture logging | no rendering change, `success==2` across 2 frames, capture logged once |

No per-draw log, no GL call injection, no manual counter manipulation.

## 5. Performance / overhead

- **Per-frame:** 0 (no log, no extra GL, no extra lock except every 300 frames).
- **Every 300 frames:** one `ov_find()` (mutex lock <1 µs) + one `log::message` (bounded, existing path). Negligible vs draw budget (`274 ms` benchmark is mock GL not driver).
- **Once per context:** two extra `log::message` (build + capture). Bounded.
- **Memory:** 5 extra atomics (20 bytes) + 1 bool per `overlay_gpu` (padded). Negligible.

No locks held across rendering, no `wglMakeCurrent`, no `glGetError`/`CheckFramebufferStatus` added in composite.

## 6. Confirmation Direct unchanged

- `git diff -- source/chams_gl.inl` = 0 lines.
- `chams_overlay.inl` changes are composite/capture logging + disabled classification only; `draw_chams()` / classifier / wall / fork paths untouched.
- Mock suite includes Direct Strict/Wide, hysteresis, fork — all PASS.

## 7. Remaining unknowns

- Actual HGLRC/HDC/thread values on user's machine (requires next Windows run with these diagnostics; counters alone insufficient).
- Whether BlueStacks uses multiple HDCs per swap (now observable via `hdc=%p` in logs).
- Whether `disabled` continuous growth after menu close is `render_mode`, `not_enabled`, `menu_open`, or `no_effect` — now distinguishable via `[no_context ... no_effect]` breakdown.
- `g_overlay_open` atomicity vs rendering thread (observed via `menu_open` counter; no fix in this stage).

## 8. Next step for user

Run updated DLL on Windows, trigger Overlay, share the 3 log lines above (build, capture, composite). No other action. No commit/push until approved.

**STOP. Await approval.**
