# Host caller probe R1 — bounded, opt-in module attribution (2026-11-12)

R1 is a **bounded revision of `source/chams_host_caller_probe.hpp` only**. It is not a
guest/host bridge, does not identify players or renderers, and does not change Chams
classification, enemy highlighting, UI, settings or persistence. Production behavior
with the default build is unchanged.

## Why R1 was needed

The pre-R1 probe could show `libOpenglRender.dll` frames but (a) collapsed the ReShade
proxy (`C:\Program Files\BlueStacks_nxt\opengl32.dll`, the chams fork installed by
`mod-tools\INSTALL.bat`) and the Windows system `C:\Windows\System32\opengl32.dll` into
one `opengl32.dll` label, and (b) anonymized `libaosl.dll` (present in the live 2.132.1
module snapshot) as `other`. Both defects hid exactly the frames needed to attribute
which module issues the hooked draws.

Live-module verification (user-reported, 2026-11-12): the **loaded** `libOpenglRender.dll`
is version `5.22.261.1001` with SHA-256
`E1E2D75548CC81CB15DD3B1ACA0D82E249CC897462F99BB04E9379F2EAE348B8` — identical to the
archive image, so the archive disassembly (including the unattributed GLES1 candidate at
RVA `0x3646c0` and the unattributed dispatch slot-looking bytes at `0xbd45d0`) describes
this live image. `libaosl.dll` is version `1.0.4`, SHA-256
`2AD6B50D2AE6591BB4800EB11A9C6C9BE6AF16E9D843A290F696D3023422E1A1`.
The two `opengl32.dll` files and `nvoglv64.dll` were **not** re-verified; treat any
statement about them as unverified. An unrelated pasted link was not downloaded or used.

## What R1 changes

1. **Allowlist additions (fixed literals only):** `libaosl.dll`, `HD-Astcdecoder.dll`,
   `glslang.dll`, `spirv-cross-c-shared.dll`. Every other non-allowlisted module is still
   reported as `other`; unresolvable frames as `unresolved`.
2. **Proxy vs Windows `opengl32.dll` split by handle comparison, never by path or name:**
   the fork's own module (`self_module()`, resolved via `GetModuleHandleExW(FROM_ADDRESS)`
   on a function inside the probe) is labelled `reshade_proxy_opengl32.dll`. An
   allowlisted `opengl32.dll` frame whose module is *not* the proxy is labelled
   `opengl32.dll(win)` when the proxy is known, `opengl32.dll` (as before) otherwise.
   No paths, base addresses or absolute addresses are written.
3. **Everything else is unchanged:** `sampler(512, 128)` (1-in-512, ≤128 samples per
   capture session), 24 frames max, module-relative `return_rva` only,
   `correlation_status="guest_unobserved"`, emission via the existing capture recorder,
   `guest_unobserved` semantics, failure ⇒ counted loss, never a crash. No ownership,
   threshold or decision logic is touched.

## Record format (unchanged shape, two new labels)

`host_caller_sample` records carry `draw_id`, `ordinal`, `sample`, `sampling_interval=512`,
`sample_limit=128`, `returned_frames`, `stack_may_be_truncated`,
`correlation_status="guest_unobserved"`, and `frames[]` of `{module, return_rva}` or
`{"module":"other"|"unresolved"}`. New `module` values that can now appear:
`reshade_proxy_opengl32.dll`, `opengl32.dll(win)`, `libaosl.dll`, `HD-Astcdecoder.dll`,
`glslang.dll`, `spirv-cross-c-shared.dll`. Tools/consumers that matched on the old
`opengl32.dll` label must account for the split.

## Build and activation — opt-in, default build unchanged

- **Default `chams-64bit` artifact is byte-identical to before R1** — the probe is still
  compiled out unless `CHAMS_HOST_CALLER_PROBE` is defined at build time.
- **CI probe artifact:** workflow `chams-64bit` job `build-probe` (runs only on push to
  `agent/chams-investigation-build` and manual `workflow_dispatch`) builds
  `msbuild ReShade.sln /t:ReShade /p:Configuration=Release /p:Platform=64-bit /p:CHAMS_HOST_CALLER_PROBE=1`
  and uploads artifact **`ReShade64-chams-probe`** (`bin/x64/Release/ReShade64.dll`).
  The `ReShade.vcxproj` change only adds a conditional preprocessor definition; the
  default configuration passes `'$(CHAMS_HOST_CALLER_PROBE)'!='1'` and defines nothing new.
- **Local probe build:** same msbuild command from a "x64 Native Tools" prompt.
- **Install & run (on the user's machine, when ready):** install the probe artifact
  exactly like the normal fork (`mod-tools\INSTALL.bat`, folder `C:\Program Files\BlueStacks_nxt`,
  it backs up the previous `opengl32.dll`), then start runtime tracking from the Chams
  menu as usual. **D3 (approved 2026-11-12): the probe build self-activates — no
  environment variable is used or required.** Sampling still happens only while
  tracking ("Start tracking") is active and draws pass the normal capture gates.
  To run without the probe, install the default `ReShade64-chams` artifact instead.
- **Expected output:** ≤128 `host_caller_sample` records per session, ≲2 KB each
  (≲256 KB total, typically far less), interleaved in the normal capture session folder.
  No effect on Chams decisions, overlay, budget or enemy Chams. Record losses, if any,
  are reported through the normal capture loss path.

## Limits (unchanged and R1-specific)

Inlined probe/scope/hook chain collapses to a few proxy frames; the stack walk may stop
early inside emulator thunks lacking unwind info (`stack_may_be_truncated` does not detect
this); RVAs are meaningless without the exact live image (now hash-verified for
`libOpenglRender.dll` only); 512:1 sampling misses rare paths; timing on the render
thread changes slightly; nothing here observes the guest renderer, transport boundary,
process boundary, ownership, or whether a draw produced pixels. A captured caller chain
is evidence of **which host module called the hooked export** — nothing more. Do not use
it to change Chams classification. Establishing a GLES2 route additionally requires
mapping repeated sampled RVAs onto the hash-verified live `libOpenglRender.dll`; ownership
remains unproven regardless.

## Test coverage

`tests/chams_host_caller_probe_test.cpp` (synthetic) covers the gate, bounded sampling,
draw-id linkage, the new labels (`reshade_proxy_opengl32.dll`, `opengl32.dll(win)`,
`libaosl.dll`), privacy (no paths/unknown names), concurrency and session reuse via
`tests/fake_windows/windows.h`. `tests/chams_host_caller_windows_test.cpp` (real Win32
APIs, CI `build-probe` and default `build`) additionally asserts the handle-comparison
split (`self` module ⇒ proxy label; `kernel32.dll` keeps its name; `nullptr` ⇒ nullptr).
A passing test proves mechanics only — never in-game behavior.

## Option A diagnostic log line (approved; R1-A, 2026-11-12)

The probe now logs, **exactly once per process at the first draw that reaches
`sample()`**, a single ReShade log line:

`[CHAMS] host caller probe: build=R1-A enabled=<1|0>`

- `enabled=1` — in current builds (D3, approved 2026-11-12) the probe build
  self-activates and always prints `enabled=1`; the gate is open and draws
  will be sampled once tracking is active.
- Historic note (R1-A/2026-11-12): the line used to report whether
  `CHAMS_HOST_CALLER_PROBE=1` was visible in the process environment; it is no
  longer influenced by any environment variable.
- `build=R1-A` — identifies the R1 probe artifact with the Option A line.

The line is compiled only when the probe is compiled (`CHAMS_HOST_CALLER_PROBE`
defined) — **the default production artifact logs nothing**. Activation logic,
sampling behavior and the log failure path are unchanged; the log failure path
is swallowed and can never affect rendering. `ReShade.log` is written next to
`HD-Player.exe` (i.e., in `C:\Program Files\BlueStacks_nxt\ReShade.log` when the
fork is installed there; the exact path is printed in the log itself at startup
and shown in the Chams menu). To check activation, launch BlueStacks, play a
few seconds, and read the line — no capture is required for this check.

## D1/D2 gate-boundary diagnostics (approved 2026-11-12)

Same gate as the R1-A line: compiled only into the probe build
(`CHAMS_HOST_CALLER_PROBE`); the default production artifact contains none of
this. Activation logic, env handling, gates and sampling behavior are
unchanged; every line below fires at most once per process and swallows its
own failures.

D1 lines (PID/TID included because one ReShade.log can collect lines from more
than one HD-Player instance):

- `[CHAMS] probe-gate entry: pid=<n> tid=<n>` — first wrapped GL draw passed
  hook entry; proves the draw hooks run in this process.
- `[CHAMS] probe-gate g1: enabled=0 pid=<n> tid=<n>` — `runtime_enabled()`
  returned false; tracking is not active in this process (never started,
  start failed, or the menu belongs to a different process than the GL
  renderer). No `host_caller_sample` records will follow.
- `[CHAMS] probe-gate g2: begin_draw=0 pid=<n> tid=<n>` — tracking was active
  but the draw was rejected at the generation/flight gate (stop/start race).
- The R1-A line above is unchanged and still marks `sample()` entry.

D2 record (`%TEMP%\reshade-log-target.txt`, overwritten on every proxy load):

`time=<yyyy-mm-ddThh:mm:ss> pid=<n> tid=<n> self="<proxy path>" log="<resolved path>" open=<0|1>`

- `open=1` — the log file at `log=` was opened; all ReShade lines for this
  process (including D1) went there.
- `open=0` with `log=""` — logging disabled in config or no log path resolved.
- `open=0` with `log=<path>` — the open failed (e.g. read-only folder or
  another instance holding ReShade.log); the process falls back to
  `ReShade.log1..log10` in the same folder or loses logging silently.
- The record is written even when the ReShade log cannot be opened, which
  distinguishes "line was never emitted" from "line was emitted into a
  different sink". It contains no environment contents, config values or user
  data; embedded quotes/control characters are replaced to keep it one line.

## D3 — probe build self-activates; no environment gate (approved 2026-11-12)

The runtime environment-variable gate (`CHAMS_HOST_CALLER_PROBE` read via
`GetEnvironmentVariableW` in `caller_probe::enabled()`) is removed. In builds
compiled with `CHAMS_HOST_CALLER_PROBE`, `enabled()` returns `true`
unconditionally; no environment variable is read anywhere on the probe path.
Behavior otherwise unchanged:

- sampling still requires: tracking active (`Start tracking` from the Chams
  menu), a draw passing both capture gates, `ticket.id != 0`, the existing
  512-draw sampling interval, and the 128-sample per-session limit;
- all probe code remains behind `#if defined(_WIN32) && defined(CHAMS_HOST_CALLER_PROBE)`;
- the default production build is unchanged and still contains none of this;
- the R1-A log line (`build=R1-A enabled=<1|0>`) is unchanged and now always
  reports `enabled=1` in the probe build;
- D1/D2 diagnostics are unchanged.

Tests: `tests/chams_host_caller_probe_test.cpp` now runs with the fake
environment reporting no variable and asserts (a) a draw is sampled, (b) the
environment is never read (`fake_env_reads == 0`); `tests/run_host_caller_probe_tests.py`
runs it once. `tests/chams_host_caller_windows_test.cpp` removes the variable and
asserts absence before sampling. Sampling limits, stale-session and concurrency
coverage are unchanged. Rationale: the env gate could not be made reliable
through the BlueStacks launcher/singleton chain; activation is no longer a
per-machine lottery. Use the default artifact to run without the probe.
