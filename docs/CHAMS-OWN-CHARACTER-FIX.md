# Own-Character Detection — Root-Cause Analysis and Fix (capture packet 2026-11-12)

Branch: `agent/chams-investigation-build`
Input: the complete 2026-11-12 capture packet (`summary.7z`, ~187 MB uncompressed):
`summary.json`, `capture-000001/2/3.jsonl`, `draw-id-1-records.txt`.

**This document was written before any code change. It separates verified facts
(from the packet) from hypotheses, identifies the root cause, and states the
smallest safe fix and its expected side effects.**

---

## 1. Packet audit (complete, not selected records)

| Fact | Value |
|---|---|
| Records written | 307,802 (attempted = queued = written, `dropped` = 0, `io_error` = false) |
| Frames / presents | 750 `frame_begin`; 747 `present_begin`/`present` (context 1 only) |
| Global frames observed by tracker | 647 (`legacy_global_frame` 11502–12148) |
| Contexts | 3 (1 = presenting/UI, 2 = main scene renderer, 3 = minor); threads 1–3 |
| Draws | 86,234 (`glDrawElements` 64,287; `BaseVertex` 15,971; `Arrays` 5,976) |
| `complete` | `false` — explained: the session was stopped mid-frame; 3 `session_stop_incomplete` frame_end records at the tail. 0 records were dropped; data for presented frames is intact. |
| `draw-id-1-records.txt` | 22,245 records (11,111 `draw_begin` + 11,111 `draw` + 23 `host_caller_sample`), all with leading-digit-1 `draw_id`. **Verified: every record is byte-equivalent to the same `seq` in the JSONL parts.** It is a filtered slice, not extra evidence. All conclusions below come from the full JSONL. |
| Decision counts (all draws) | `primitive_or_count` 42,148 · `classifier_reject` 24,910 · `menu_open` 11,495 · `direct` 7,025 · `hide_own` 656 |
| Character-classifier-matched draws | **7,681 — all on context 2 / thread 2** (`direct` 7,025, `hide_own` 656). Contexts 1 and 3 have zero character draws. |

### Context-local `frame` freeze is a capture artifact, not the runtime bug
All context-2/3 draw records are stamped `frame: 1` because the capture increments
its per-(context, surface) counter only on `present`, and presents happen only on
ctx1 (`frame_end` ctx1: 747, ctx2/ctx3: stuck at 1). The runtime's own-estimate
tracker is **per-thread**, fed by `g_frame_id` (incremented in `begin_frame()` at
`wglSwapBuffers`), and resets its caches when that atomic changes — verified by
reading `touch_frame()`/`update_own_estimate()`. In this packet the drawing thread
(thread 2) logged 607 tracker updates spanning the whole 647-frame session with no
sign of a mid-run native-context recreation, so the documented cross-context
frame-reset did **not** destroy the estimate during this capture (§5).
Analysis therefore correlated draws to global frames via the `seq`-ordered
`own_tracker` records (thread 2).

## 2. Verified facts about the two populations (the whole basis of classification)

### Own model — programs 276, 277, 278, 279 (present in 599/599 frames with character draws)

Per frame, 11–12 character draws, at these world-space origins (identical every frame):

| Program | Draws/frame | World translation | Origin distance from camera |
|---|---|---|---|
| 278 (body/cloth/hair) | 7 | 1 draw near-bone origin oscillating 2.58→2.96; 5 draws at root **(58.0046, 11.4363, 30.1556)**; 1 far-bone origin 3.1–3.4 | root constant **3.037654** |
| 276 (part) | 1 | root (58.0046, 11.4363, 30.1556) | root constant 3.037654 |
| 277 (part) | 1 | root (58.0046, 11.4363, 30.1556) | root constant 3.037652 |
| 279 (part) | 1 | root (58.0046, 11.4363, 30.1556) | root constant 3.037654 |

The own-model **root origin is bit-identical in all 599 frames** (the player stood
still during the capture). Distance breakdown across 7,681 character draws:

```
2.92 m: 934   3.03–3.04 m: ~1,740   2.8–2.86 m: ~754   2.6–2.79 m: ~1,200
```
(8–9 of 11 own draws per frame sit at the constant root distance; two animated bone
origins oscillate and periodically swing toward the camera.)

### Enemies — program 206 (1,222 draws, 518 frames)
World translations (70.5–71.8, 11.4–11.5, 53.4–59.1) — three stationary instances,
typical distances 27–41 m. Distance range across the whole capture:
**min 3.674 m** (exactly 5 draws below 5 m: 3.674, 4.029, 4.099, 4.419, 4.934).
Enemies are never closer than 3.674 m here.

### `host_caller_sample` / `libOpenglRender.dll`
All 128 host-caller samples resolve through `reshade_proxy_opengl32.dll` →
`libOpenglRender.dll` (guest EGL context presentation path), `
correlation_status: guest_unobserved` for every recorded sample. Loader/host
identity, unchanged from prior packets.

## 3. What the tracker actually did (thread 2, 607 updates, lgf 11502–12148)

- Learned `own_est` peaks at **2.862** (never reached 2.838+ except briefly at
  lgf 11518–11521), because `frame_min` — the quantity it learns from — is the
  distance of the **nearest animated bone**, which oscillates 2.58→2.96 and
  periodically swings to **0.83–1.50 m** (lgf 11609–11626, 11970–12004, a
  gun-switch/ADS-class transient).
- Every frame whose minimum fell outside [1.5, 5.0] triggered a **full estimate
  wipe** (`out_of_range`, 43 events on thread 2, all < 1.5 m); every frame with
  zero comparable character draws wiped too (`no_observation`, 8 events on thread 2);
  27 `candidate_changed` resets dropped `stable` to 1.
- Hiding requires `stable ≥ 30` at 0.3-lerp ramp-up (~30 frames).
  **431 of 599 character frames were in the `stable < 30` re-acquisition window;**
  during those frames `hide_own` could not fire at all (5,473 of the 7,025
  `direct` draws occurred there).
- Even with `stable ≥ 30` (e.g. lgf 11518–11521), the exclusion window
  `|d − est| ≤ 0.2` around est≈2.82–2.86 (2.62–3.06) only owned the near-bone
  matches; the 8–9 root draws at 3.038 m were covered only while est ≥ 2.838 —
  exactly the 4 frames above (9 hides each).
- Observed result: `hide_own` fired only **656 / 7,681** character draws (8.5%);
  the rest of the own model got full enemy chams every other frame — the reported
  "own chams visible / flickering".

## 4. Root causes (all data-backed)

**A. Wrong estimator for a multipart asset.** The own estimate is learned from
`frame_min`, a single animated origin that oscillates ±0.4 m frame-to-frame. The
invariant in this asset is the *root cluster* (8–9 draws at a constant distance),
not the minimum. The tracker never learns the root distance reliably.

**B. Single-sample outlier → total forget.** One implausible sample (< 1.5 m, the
animated bone) wipes `cand/stable/own_est` to zero and imposes ~30 frames of
re-acquisition. 43 such wipes in 11 s of capture; the estimate (and therefore all
own hiding) vanished precisely during weapon-raise/aim transients and loading.

**C. Exclusion window anchored to the laggard.** Even when stable, est trails the
bone (`0.7·cand + 0.3·min` per frame) and never covered the root cluster except in
cheap windows (est ≥ 2.838 occurred only ~4 frames of 599).

Consequence of A+B+C: the bulk of the own silhouette is rendered as if it were an
enemy, with multi-second dropouts of protection — exactly "visible / flickering".

**Not the cause (per the complete evidence):** the cross-context / unrelated-present
frame reset. The `frame` freeze for ctx2/ctx3 is a capture-side artifact (presents
only on ctx1); the runtime tracker is per-thread and keyed to `g_frame_id`, and this
packet shows no mid-session context recreation. The per-thread reset semantics for
genuine zero-draw frames are kept (§6, safety). Enemy programs use
`MATRIX_VP_CAMERA` (camera uniform missing) while own programs report
`_WorldSpaceCameraPos`; distance meaning is consistent (both Euclidean), so the
`MATRIX_VP` exclusion guard is untouched.

## 5. No reliable local-player identity signal exists in this packet

Verified: GL program/buffer/vao names are equal for all instances of a program;
shaders cannot distinguish own from enemy skins; the constant-distance root is also
constant for the *stationary* enemy instances (32.107 / 33.0xx m); static ARM64
offsets are not usable at runtime; screen position, bone transforms and mesh IDs are
not ownership evidence and are not recorded as such. The camera-relative offset is
not recoverable here (`view` is zero for every character draw; only
`view_projection` is valid). **Classification cannot be made identity-exact from
this data.** The fix therefore improves the *estimator* and the *outlier policy*;
it does not claim ownership. The residual irreducible ambiguity (a perfectly
stationary enemy standing within 0.2 m of the own root distance for 30+ consecutive
frames, below the 5.5 m own cap) persists — it is strictly smaller after the fix
because the window is centered on the true 3.038 m root instead of the lagging bone
estimate, and the window top (est+0.2 ≈ 3.24 m) still leaves the
closest observed enemy draw (3.674 m) outside by 0.43 m.

## 6. Proposed minimal fix (no threshold widening, enemies untouched)

All changes confined to the own-estimate tracker in `source/chams_gl.inl`
(`own_track`, `update_own_estimate()`, plus one capture-record field addition):

1. **Per-frame samples, not just the minimum.** Character draws with comparable
   distance (non-`MATRIX_VP`, dist > 0) also append to a fixed 64-entry
   `frame_obs` buffer (`frame_min` logic kept for the capture record, unchanged).
2. **Modal observation.** At frame end, the observation is the sample with the most
   neighbours within ±0.05 m, restricted to the existing [1.5, 5.0] plausible range,
   requiring **≥ 3 votes**; with fewer than 3 plausible samples, fall back to the
   minimum plausible sample (preserves sparse-frame behavior, including the current
   unit tests that feed single distances).
3. **Outlier frames no longer wipe the estimate.** If samples exist but **all are
   < 1.5 m** (the observed gun-switch/ADS signature), the update is *skipped*
   entirely (`reason: outlier_skipped`); `cand/stable/own_est` are preserved and
   hiding continues uninterrupted. If **all samples are > 5.0 m** (nothing near the
   camera), the current full-wipe behavior is kept (`out_of_range`). If there are
   **zero samples** (no comparable character draws at all), the current full-wipe
   behavior is kept (`no_observation`) — death/spectate safety is unchanged.
4. **Unchanged:** `own_distance_excludes` (`|d − est| ≤ 0.2`, `stable ≥ 30`,
   `distance < own_cap` 5.5 m, `MATRIX_VP` guard), the 0.2 tolerance, the 5.0/1.5
   plausibility bounds, the 0.7/0.3 lerp, `k_min_index_count`, the classifier, all
   glow/stroke/wall passes, settings, persistence, UI, and default production
   behavior outside the tracker.
5. **Capture record** gains two additive fields (`mode`, `mode_votes`); existing
   fields/reasons are untouched (`frame_min` still logs the true minimum).

### Regression behavior of replayed capture (offline replay of all 7,681 character draws)

| Metric | Current logic | Proposed logic (faithful replay) |
|---|---|---|
| Final est | 2.68 | **2.92** |
| Character draws excluded | 1,085 (replay; 656 in capture) | **3,871** |
| Frames with hiding available | 246 / 599 | **422 / 599** |
| Enemy (prog 206) draws excluded | 0 | **0** (nearest enemy 3.674 m > window top ≈ 3.12 m) |
| Exclusions by program | own 276/277/278/279 only | own 276/277/278/279 only (450 / 450 / 2,521 / 450 — 59.9% of 6,459 own draws) |
| Near-camera swing frames (all <1.5 m minimums) | 28 full estimate wipes | **28 skips (no wipe, hiding persists)** |

Expected side effects / limits (stated honestly):
- ~60% of own-model draws are excluded consistently instead of ~11%; the
  animated near-bone and far-bone parts may still leak chams when outside the
  0.2 m window (no tolerance widening is applied), and 28 swing frames no longer
  reset progress.
- Enemy hiding margin shrinks from 0.79 m to ≈ 0.66 m in this capture
  (nearest enemy 3.674 m vs window top est+0.2 ≈ 3.12 m); an enemy stationary at
  ≤ 3.12 m for 30+ consecutive frames under the player's camera would still be
  excluded — the same irreducible ambiguity as today, strictly reduced, never
  worsened for observed enemies.
- During all->5.0 m frames and zero-draw frames, the estimate is still forgotten
  (as today); hiding resumes only after 30 stable re-acquisitions.
- Cost: +64 floats/thread of tracker state, ≤ 64² inspections once per frame
  on the frame-change path (negligible; not per draw).

### Cross-context frame reset
Not changed by this fix: evidence shows it did not contribute to this flicker
(presents happen only on ctx1; the own tracker lives on the drawing thread; no
context recreation observed). The related **zero-draw wipe** it relies on is kept
deliberately. Genuine mid-session context recreation would still reset
`own_track` on the next draw on that thread (existing, conservative behavior).

## 7. Validation plan

- Offline replay above (this document).
- Updated unit expectations in `tests/run_mock_gl_tests.py` that encoded the old
  wipe behavior (single-frame `out_of_range` no longer wipes; sparse frames keep
  nearest-plausible learning; zero-draw frames still wipe) + new regression block
  replaying a full capture-class sequence: 11 own-part draws/frame at constant
  root distance + oscillating bone + gun-switch swing (bone at 0.83–1.0 m)
  + nearby enemy at 3.674 m — asserts acquisition, uninterrupted exclusion through
  the swing, exclusion of the root cluster, non-exclusion of the enemy, and wipe on
  genuine zero-draw frames.
- All existing suites must pass unchanged (enemy highlighting, overlay, capture,
  probe, lifecycle). No production installation performed; no capture requested.
