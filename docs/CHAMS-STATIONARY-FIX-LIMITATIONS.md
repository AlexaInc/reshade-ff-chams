# Stationary-camera follow-up: partial corrections and limitations

This change does not implement or validate reliable player ownership or wall identity. It must not be represented as a complete Chams fix. See the accompanying stationary-capture investigation outside the repository for the evidence.

Recorded stationary capture seq 55062/55064 shares world and VP matrices but reports 3.486254 Euclidean versus 3.329197 clip-w, with different own-exclusion decisions. `object_distance` now derives a finite perspective centre from the VP matrix and uses Euclidean camera-to-origin distance. If a centre cannot be recovered, the existing clip-w fallback is retained for rendering but excluded from own-distance tracking/comparison. Orthographic/singular, ill-conditioned and non-finite VP are rejected by the centre recovery.

Own estimate, per-program caches and viewport cache are now thread-local. A context switch on a thread resets its estimate and program caches; migration to another thread begins with a fresh state. The global swap epoch is atomic, preventing that counter's data race. These are targeted corrections, **not** proof that a swap is a game frame. GL context handle/address reuse, program relinks, context lifecycle and thread migration are not fully modeled. The still-global budget, settings, wall and overlay state have separate synchronization and lifecycle limitations. Frame intervals without observations still reset the estimate; context switches can also require reacquisition.

The own tracker still uses an individual draw origin, global swap-derived epochs, nearest-distance statistics and tolerances. It does not establish a stable player object or associate all child draws, and it can both paint the local player and exclude a nearby stationary opponent. Perspective-centre recovery does not fix differences among part origins. Old clip-w fallback is no longer used for own matching, but Euclidean distance affects the existing outline sizing calculation. Camera/data failure therefore needs runtime testing. No left-screen-side rule or hardcoded program, texture, buffer or wall blacklist was introduced.

Mock regression fixtures include capture seq 55062/55064 and the school pair 33655/33657; tests exercise thread isolation, context switching, singular/NaN fallback and existing GL state/config/capture behavior. They do not test a real GL driver or pixels and cannot verify enemy/local identity. Gloo Wall identification remains unsolved; wall hints did not fire in the supplied captures, and no synchronized pixel/draw label establishes wall geometry. No wall heuristic change was made.

A deployable release still requires a successful exact-commit Windows x64 build and artifact verification. Even then, in-game tests are required for own ON/OFF, local plus nearby enemy across camera angles, lobby/match, Strict/Wide, both renderers, and Gloo Walls. Accurate ownership needs further labeled, frame-correlated local/enemy observations and an observed game-context/present handoff, not an assumed fixed world transform or a screen side.

## Shared native-context correction (WIP)

`wglMakeCurrent` may assign the same ReShade device wrapper to multiple WGL
contexts that share resources. `touch_frame` now checks the native WGL context
as well as the wrapper before reusing its thread-local tracker and caches.
The mock regression covers two native contexts sharing one wrapper, unchanged
context, a return to the first context, and wrapper replacement. Context
switches still reset rather than migrate the tracker. A destroyed and recreated
native handle reused between draws is not separately generation-tracked.
This correction does **not** establish a game-frame boundary: the legacy global
presentation counter still drives `update_own_estimate`, and unrelated-context
presents can therefore still reset the active tracker. It also does not identify
players or distinguish nearby opponents or Gloo Walls. Real WGL overhead and
BlueStacks visual behavior have not been measured.
