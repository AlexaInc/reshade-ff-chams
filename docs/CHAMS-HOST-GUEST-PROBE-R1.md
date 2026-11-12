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
  it backs up the previous `opengl32.dll`), then set the environment **before BlueStacks is
  started**: `setx CHAMS_HOST_CALLER_PROBE 1` (new terminals only; for same-session use
  `cmd /c "set CHAMS_HOST_CALLER_PROBE=1 && start \"\" \"C:\Program Files\BlueStacks_nxt\HD-Player.exe\""`
  or launch BlueStacks from an environment where the variable is set). Start runtime
  tracking from the Chams menu as usual. Deactivation: `setx CHAMS_HOST_CALLER_PROBE /d`.
  Without the variable the probe does nothing at runtime.
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
