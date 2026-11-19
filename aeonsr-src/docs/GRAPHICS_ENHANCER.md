# Graphics Enhancer by AlexaInc

Fork of AeonSR providing **DLSS / FSR / XeSS temporal upscaling + optical flow on any API**.

### AlexaInc Changes
1. **Catalog Panel UI** — collapsible numbered panels (01 Master … 09 Logs) matching Figma `GpQvYBzm8d1wet9mdajwvY` with mint toggle.
2. **Header Branding** — top banner with AlexaInc logo (`res/alexainc_logo.png` -> 42x42 rounded, gradient cyan->navy) and `EXPAND ALL / COLLAPSE` controls.
3. **FPS Fix** — `source/runtime.hpp: _show_fps = 1` (was 2) so OSD shows even when ReShade overlay closed.
4. **Chams coexistence** — tested alongside `reshade-ff-chams` (overlay =1 or 3) — both use ReShade OSD window `OSD`; positions are shared via `_fps_pos`.

### Build
Same as upstream:
```ps
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
Artifacts: `AeonSR.addon64`, `AeonSRHost.exe`, `ngxshim/nvngx.dll`, `runtime/` — copy next to game exe alongside `ReShade64.dll` (chams). Do not mix `nvngx.dll` with ReShade’s `opengl32.dll`.

### Legal
Upstream MIT retained. New files © AlexaInc. Vendor runtimes (DLSS, FSR, XeSS, Agility SDK) keep their licences in `THIRD-PARTY-NOTICES.md`.
