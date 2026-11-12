# Own-character Chams flicker — implementation summary (2026-11-12)

Build target: `ReShade64.dll`, Free Fire MAX 2.132.1 / VC 2019118525 (ARM64 instance on BlueStacks; the DLL itself is an x64 Windows proxy — the build is ABI-independent of the game).
Branch: `agent/chams-investigation-build`. This document is the fix summary — it is not a new investigation report. Evidence for every claim lives in `CHAMS-OWN-CHARACTER-FIX.md`, `NATIVE_2019118525_ANALYSIS.md`, `SHADER_NATIVE_CORRELATION.md` and the capture packets referenced there.

## What was fixed (this change)

The own-character exclusion (`own_distance_excludes`) suppresses Chams for the local player. The pre-fix tracker learned a single distance (`frame_min`, the nearest animated bone origin), wiped the whole estimate on any out-of-range frame, and anchored a narrow window on it — so ADS/near-camera bursts destroyed hiding for tens of frames.

The working-tree fix (this commit) has two parts:

1. **Multipart modal observation (pre-implemented in the tree, preserved & completed):**
   - per-frame capture of *all* comparable character-draw distances (`frame_obs[64]`);
   - the observation is the modal root-cluster distance (≥3 votes within ±0.05 m) — the persistent 3.04 m invariant instead of the oscillating bone minimum;
   - sparse frames (1–2 plausible samples) keep nearest-plausible learning at cold start; all-near bursts (ADS/gun switch) **skip the update** (`outlier_skipped`) instead of wiping the estimate; zero-draw and all-far frames still wipe (death/spectate safety, unchanged).
2. **Sparse-frame corroboration gate (new, this pass):**
   - with an established reference (`own_est` or `cand`), a sparse frame is learned **only** when some plausible sample agrees with the reference (|Δ| ≤ 0.3 m, the same corroboration band already used for candidate rebasing);
   - a lone distant sample (typically an enemy at 3–5 m while own forks are missing — e.g. a weapon fork hidden for a frame or culled) now yields `skip` — **it can neither rebase the candidate nor reset `stable`**;
   - without an established reference (cold start) the legacy nearest-plausible fallback is preserved.

Enemy classification, the `±0.2` exclusion window, the `stable ≥ 30` gate, the 1.5–5.0 m plausibility band, `own_cap`, and all rendering passes are **unchanged**. No distance threshold was widened. Enemy handling is, if anything, slightly more conservative (enemies get fewer rebasing opportunities).

## Verification (mock-GL unit tests, `tests/run_mock_gl_tests.py`)

New regression block (`sg_*`) asserts: acquisition (31 frames → `stable==31`, est≈3.038); lone enemy-only sample at 4.5 m and 4.9 m → est/candidate/stable untouched; lone corroborating sample at 3.038 m → stable increments; corroborating + enemy pair (3.04 / 4.8 m) → stable increments, est anchored to the root; **nearby enemy at 3.674 m is never excluded**; ADS/all-near 6-frame burst → estimate preserved and exclusion still applies; cold-start sparse bootstrap unchanged; weapon-switch gap (≥3 plausible, no cluster) → estimate preserved; recovery after an honest zero-draw wipe works and the 3.674 m enemy stays visible.

All pre-existing mock tests pass (the `XFAIL` lines are the documented known-unfixed cases, unchanged and still tracked). Verified locally with `python3 tests/run_mock_gl_tests.py` plus the other ubuntu CI legs (`run_lifecycle_hook_tests.py`, `check_capture_integration.py`, `run_host_caller_probe_tests.py`, `test_bridge_coverage.py`, `run_chams_capture_tests.py`, config-validation `g++` build → exit 0). Windows-leg tests run in CI on push.

The Gloo Wall detection issue from `CHAMS-STATIONARY-FIX-LIMITATIONS.md` is **not** addressed here and remains documented as unresolved — no wall heuristic was touched, and the fix must not be read as fixing wall detection.

## Installation

**Not authorized yet.** Building produces a new artifact; installing it into BlueStacks and gameplay capture require separate explicit approval. Do not install from this commit without it.
