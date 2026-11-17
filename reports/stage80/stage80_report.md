# Stage 80 — Stage 79 Static Audit & Windows Build Readiness

**Status: AUDIT ONLY. No source modifications. No commit/push. STOP.**
**Baseline:** `038fa1a`, branch `agent/chams-investigation-build`, Stage 79 uncommitted (3 files, 201+/7−).

## Executive Summary

| Area | Verdict |
|---|---|
| Context identity | **PASS** (`ov()`/`ov_find()` use current HGLRC; cross-ctx safely rejected) |
| Thread safety | **PASS with MEDIUM risks** (rehash safe, stale HGLRC reuse, lock scope) |
| Capture/fallback | **PASS** (`has_mask` only after success; Direct fallback preserved; no stale reuse) |
| Mock quality | **PARTIAL** (6/8 genuine; Tests 4/8 counter-only, not failure simulation) |
| Windows build | **LIKELY PASS, NOT VERIFIED** (no blockers found, no MSVC build) |
| Direct preservation | **PASS** (`chams_gl.inl` 0 lines, fallback unchanged) |

**Defects:** 0 HIGH, 2 MEDIUM (stale HGLRC, lock scope), 3 LOW (mock gaps, log, hook). **No silent fixes.** Awaiting approval.

---

## 1. Context Identity Audit

**Verified facts:**
- `current_context_key()` (`chams_overlay.inl:77-85`): Windows `wglGetCurrentContext()`, mock `g_opengl_context`. `ov()` (:105-109) and `ov_find()` (:111-115) both call it, lock mutex, lookup map. **Always current HGLRC.** ✓
- Capture (`overlay_capture_mesh` :332, `ov()` :334) and composite (`render_overlay` :402, `ov_find()` :428) use current key at call time. Different HGLRCs → different entries. Composite on ctx B with no entry → `nullptr` → `comp_ctx_mismatch` + skip. **No invalid reuse.** ✓
- `ov()` callers: `overlay_build` (:190), `overlay_ensure_size` (:294), `overlay_capture_mesh` (:334), `observe_overlay_menu` (`chams_gl.inl:1990`), mode switch (`chams_gl.inl:2270`), capture check (`chams_gl.inl:2442`). All use current key. Composite uses `ov_find()` (no create). **Composite never builds in swap** (preserves no-stall). ✓
- Capture/composite can run on different contexts/threads: Draw thread (capture) vs swap thread (composite) may differ (Stage 76 UNKNOWN). Per-context map handles: different keys → different entries, composite skips if no entry. **Safe.** ✓
- Context switching while entry in use: `ov()` returns `overlay_gpu&` under lock, lock released, caller uses without lock. WGL ctx thread-affine (one thread per HGLRC at a time) → same HGLRC not accessed concurrently. Different HGLRCs → different entries. **Safe under WGL assumption.** (See §2 for lock-scope risk.)

**Inferred risks:** None for identity (map correctly isolates).

**Unknowns:** Whether BlueStacks ever makes same HGLRC current on two threads simultaneously (WGL forbids, but driver bug possible). UNKNOWN.

## 2. Resource Lifetime and Thread Safety

**Verified facts:**
- Locking: `ov()`/`ov_find()` lock `ov_mutex()` for map access. After return, lock released. Caller uses entry without lock. **Different entries concurrent safe; same entry concurrent requires WGL thread-affine.** ✓
- Rehash: `unordered_map` guarantees references/pointers to elements remain valid after rehash (only iterators invalidated, C++ standard). `ov()` reference and `ov_find()` pointer **remain valid.** ✓
- Creation/resize/access: Build/resize on draw thread, composite on swap thread. Different HGLRCs → different entries (safe). Same thread → sequential (safe). Same HGLRC different threads → impossible per WGL. **Safe.** ✓
- No sharing introduced: All per-context, no `wglShareLists` in production. ✓

**Potential defects (ranked):**
- **MEDIUM — Stale HGLRC reuse:** Map never erased (no `erase` in code). If ctx deleted (`wglDeleteContext`), entry remains with stale HGLRC key + invalid GL names. If driver reuses HGLRC value for new ctx, lookup returns stale entry (`tried=true`, `ready=true`, old FBO names) → invalid reuse on new ctx. GL errors expected, composite `GetError` check prevents false success, no crash. **Inferred risk** (HGLRC reuse frequency UNKNOWN). **Not fixed** (audit only). Recommend: Hook `wglDeleteContext` to erase entry (future).
- **MEDIUM — Lock scope:** Reference/pointer used without lock. If same HGLRC accessed concurrently (WGL violation), data race on `tried`/`ready`/`has_mask`. **Inferred risk** (requires WGL violation). Recommend: Hold lock during build/capture/composite, or document thread-affine assumption.

**Unknowns:** HGLRC reuse frequency, BlueStacks thread-affine compliance.

## 3. Capture and Direct Fallback Audit

**Verified facts:**
- Every `overlay_capture_mesh()` call: **One** site (`chams_gl.inl:2440`, inside `if (render_mode==0 && overlay_build())`). No other callers. ✓
- `has_mask` only after success: Set at `chams_overlay.inl:382-383` ONLY if `err==NO_ERROR && status==COMPLETE` after `real_draw()` (:375). Before, not set (removed from first-frame block). On failure, `has_mask=false`, `captured=invalid` (:386-387). No other `has_mask=true` (grep). ✓
- Failed capture cannot suppress Direct: `draw_chams` :2442-2444 checks `has_mask && captured==frame_id`. Failure → `has_mask=false` → `overlay_captured=false` → falls through to Direct (:2456-2459). ✓ (Stage 78 bug fixed.)
- Frame ordering/stale: `g_frame_id` incremented in `begin_frame()` (`chams.cpp:175`, called :1332 **after** `render_overlay` :1330). Capture sets `captured=frame_id` during draws (frame N). Composite checks `captured!=frame_id` (:440-444) at swap (still N). Previous frame (N-1) → mismatch → `comp_frame_mismatch` + skip. Resize/mode-switch reset `has_mask=false` → `comp_no_mask` before frame check. **No stale reuse.** ✓
- Previous frame reuse path: None. `has_mask` true only for current frame (set after draw, checked against current frame). ✓

**Inferred risks:** None (logic verified).

**Unknowns:** None (source-verified).

## 4. Mock Test Quality

**Verified facts (8 tests, `run_mock_gl_tests.py:1224-1288`):**

| Test | Genuine? | Mock can produce? |
|---|---|---|
| 1 Single-ctx capture+composite | **YES** | Custom shader/program success, `GetError=NO_ERROR`, `CheckFramebufferStatus=COMPLETE` → `has_mask` true, `comp_success`+1 |
| 2 Independent, no invalid reuse | **YES** | `g_opengl_context=2` no entry → `ov_find()==nullptr`, `comp_ctx_mismatch`+1 |
| 3 Shared, local FBO/VAO | **YES** | `Gen*` incrementing IDs → `fbo!=fbo1`, `vao!=vao1` |
| 4 Capture failure, `has_mask` false | **NO** | Comment-only: "Mock always succeeds, verify via frame mismatch." No failure produced. **Counter-only.** |
| 5 Frame mismatch, stale rejected | **YES** | `g_frame_id=201` without capture → `comp_frame_mismatch`+1 |
| 6 Context switch, correct set | **YES** | Switch 1→2→1 restores `fbo1`/`fbo2` (map preserves) |
| 7 Direct regression | **YES** | `render_mode=1`, `!has_mask` after Direct draw (minimal, but genuine) |
| 8 GL error/incomplete | **NO** | Manually sets `has_mask=false`, verifies `comp_no_mask`+1. Does NOT simulate `GetError` failure or incomplete FBO. **Counter-only.** |

**Coverage gaps (do not claim tested):**
- `glGetError()` failures: **NOT simulated** (mock `GetError` always `NO_ERROR`).
- Incomplete FBOs: **NOT simulated** (mock `CheckFramebufferStatus` always `COMPLETE`).
- Failed object creation: **NOT simulated** (mock `CreateShader`/`CreateProgram`/`Gen*` always succeed).
- Failed capture after `real_draw()`: **NOT simulated** (mock always succeeds).
- Concurrent access: **NOT simulated** (single-threaded, sequential switches).
- Stale HGLRC reuse: **NOT simulated** (no `wglDeleteContext` mock).

**Verdict:** 6/8 genuine (1,2,3,5,6,7). Tests 4/8 are **counter-only assertions**, not failure simulation. **Do not claim failure cases tested.**

## 5. Windows Build Readiness

**Verified facts:**
- Includes: `chams.hpp:61-62` adds `<mutex>`, `<unordered_map>`. MSVC C++17 supports both. ✓
- Types: `void*` key (HGLRC `struct HGLRC__*` convertible). `wglGetCurrentContext()` returns HGLRC, cast `(void*)`. `g_opengl_context` (`device_context_impl*`/`void*`) convertible. ✓
- Declarations: `wglGetCurrentContext()` inside `#ifdef _WIN32` (`chams_overlay.inl:80`). Windows declaration via `opengl.cpp` Windows headers (opengl.cpp:25 already calls it). Mock (Linux, `_WIN32` undefined) uses fallback, no declaration needed. Mock compiles (PASS). ✓
- Conditional: `#ifdef _WIN32`/`#else` correct. No new libs (all inline/STL). ✓

**Likely blockers:** **NONE identified** (static audit). `std::unordered_map`, `std::mutex`, `std::atomic`, WGL call all MSVC-compatible.

**Unknowns:** Windows build **NOT verified** (no MSVC on VM, no CI run, no commit/push). **Do not claim success without actual Windows build.**

## 6. Direct Mode Preservation

**Verified facts:**
- `source/chams_gl.inl` diff **0 lines** (`git diff` verified). Direct path untouched. ✓
- Fallback path: `draw_chams` (:2213) → mode switch (:2267-2281) → Overlay branch (:2438-2444) → Direct fallback (:2456-2459, `++direct`, Direct passes). **Unchanged.** ✓
- Indirect `ov()` changes: `ov()` calls at :1990, :2270, :2442 now per-context (vs global). Single-ctx behavior identical (same entry). Multi-ctx: per-ctx reset (intended). Direct passes themselves do NOT use `ov()` (game's program/VAO/VBO). **No Direct regression.** ✓

**Inferred risks:** None.

**Unknowns:** None (source-verified).

---

## 7. Defects Ranked by Severity

| Severity | Defect | File/Line | Status |
|---|---|---|---|
| **MEDIUM** | Stale HGLRC reuse (map never erased, HGLRC reuse returns stale entry) | `chams_overlay.inl:88-91` (map, no erase) | **Inferred risk, not fixed** |
| **MEDIUM** | Lock scope (reference used without lock, race if same HGLRC concurrent) | `chams_overlay.inl:105-115` | **Inferred risk, not fixed** |
| **LOW** | Mock Tests 4/8 not genuine failure simulation | `run_mock_gl_tests.py:1255-1288` | **Coverage gap, not fixed** |
| **LOW** | Periodic log `%300` (assumes 1 increment/frame) | `chams_overlay.inl:405` | **Verified safe** (begin_frame once/swap) |
| **LOW** | `wglGetCurrentContext` hook (calls trampoline, no recursion) | `opengl_hooks_wgl.cpp:1206` | **Verified safe** |

**No HIGH defects.** No crashes, no Direct breaks, no invalid reuse in single-ctx or cross-ctx (rejected).

## 8. Recommended Next Safe Action

1. **Do NOT commit/push yet** (await approval per restrictions).
2. **Next:** Approve commit/push for **CI Windows build verification** (mock PASS, static audit likely PASS, but must verify MSVC). No source changes needed for build.
3. **Then:** BlueStacks Overlay runtime test (after approval) to verify outline + `comp_success>0`, Direct regression.
4. **Future (not Stage 79):** Hook `wglDeleteContext` to erase map entry (stale HGLRC), add mock failure modes (configurable `GetError`/`CheckFramebufferStatus`), hold lock during use or document thread-affine.

---

*Audit only. No source modifications. No commit/push. STOP. Awaiting approval.*
