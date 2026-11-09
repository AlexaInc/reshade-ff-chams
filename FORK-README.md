# ReShade — chams fork (AlexaInc)

Base: [crosire/reshade](https://github.com/crosire/reshade) @ `7bf9de8` (2026-09-24).

This fork does exactly one thing on top of upstream ReShade: it replaces the
ReShade overlay's menu **content** with a Chams page, and adds two extra draw
passes (body highlight + 2D stroke) to the OpenGL layer. Everything else —
the effect runtime, the add-on API, the D3D9/10/11/12, DXGI, Vulkan and OpenGL
layers, input handling, screenshots — is upstream and untouched.

| File | Purpose |
|---|---|
| `source/chams.hpp` | settings struct |
| `source/chams.cpp` | `[CHAMS]` section of `ReShade.ini` (load/save) |
| `source/chams_gl.inl` | the two extra draw passes (included by `source/opengl/opengl.cpp`) |
| `source/chams_menu.cpp` | the Chams page, built with ReShade's own ImGui |
| `source/opengl/opengl.cpp` | 6 draw entry points wrapped |
| `source/runtime_gui.cpp` | overlay pages replaced, menu key = INSERT |
| `source/runtime.hpp` | `draw_gui_chams()` declaration |
| `CMakeLists.txt`, `ReShade.vcxproj` | new files added to the build |
| `.github/workflows/chams-64bit.yml` | builds `ReShade64.dll` (64-bit) on Actions |
| `mod-tools/` | `CLEAN-FIRST.bat`, `INSTALL.bat`, `UNINSTALL.bat`, notes |

## Build

Actions tab → **chams-64bit** → *Run workflow* (also runs on every push to
`main`). Artifact: **ReShade64-chams** → `ReShade64.dll`.

## Install (BlueStacks)

1. `mod-tools\CLEAN-FIRST.bat` (as administrator) — removes the files earlier
   proxy builds left behind.
2. Put `ReShade64.dll` in the same folder as `INSTALL.bat` and run
   `INSTALL.bat` (as administrator) — it installs it as `opengl32.dll` and
   keeps the previous DLL as `opengl32_before_chams.dll`.
3. BlueStacks → Settings → Graphics → **Graphics engine = OPENGL**.
4. Start BlueStacks, press **INSERT** → Chams page.

Undo: `mod-tools\UNINSTALL.bat`.
