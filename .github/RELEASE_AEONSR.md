## Graphics Enhancer by **AlexaInc** — v1.3.2 (Aeon SR fork)

Modded build of [Aeon SR](https://github.com/BarbatosAWLS/AeonSR) by **Barbatos AWLS** (MIT),
rebranded and extended by **AlexaInc**.

### What is inside

| File | What it is |
| --- | --- |
| `AeonSR.addon64` | The 64-bit add-on. This is the mod itself. |
| `AeonSR.addonfx` | Effect-syntax definition used by the ReShade overlay. |
| `alexainc_logo.png` | AlexaInc mark used in the overlay header. |

### Install

1. Install **ReShade with add-on support** for your game (this DLL is the *ReShade* half).
2. Create the folder `reshade-shaders\Addons` next to the game executable if it does not exist.
3. Copy `AeonSR.addon64` and `AeonSR.addonfx` there.
4. Start the game, press **Home** (or your ReShade overlay key), open the **Add-ons** tab,
   select **Aeon SR**.
5. The **Chams** menu (by AlexaInc) appears as its own tab when the Chams add-on is also installed.

### Vendor runtimes (DLSS / FSR / XeSS)

The upscalers need the vendor DLLs at runtime. They are **not** in this release because
NVIDIA, AMD and Intel licences do not allow redistributing them from source builds:

* `nvngx_dlss.dll`, `nvngx_dlssg.dll` — from an NVIDIA DLSS package, drop next to the game exe
  (or let AeonSR copy them from its `runtime\` folder, as upstream does)
* `amd_fidelityfx_upscaler_dx12.dll`, `amd_fidelityfx_framegeneration_dx12.dll`,
  `amd_fidelityfx_loader_dx12.dll` — from the AMD FidelityFX SDK
* `libxess.dll`, `libxess_dx11.dll`, `libxess_fg.dll`, `libxell.dll` — from the Intel XeSS SDK

If a runtime is missing, Aeon SR falls back to its own temporal upscaler, so the add-on
still loads and still anti-aliases.

### What this fork changes vs. upstream Aeon SR

* AlexaInc Graphics Enhancer branding: overlay header, dark catalog UI, mint `#94E8C1` accent
* FPS counter defaults to **always on** (upstream only shows it with the overlay open)
* **FPS counter background** — solid or frosted plate behind the counter, with colour,
  opacity, corner radius and padding (`[OVERLAY] FPSBackground`, `[STYLE] ColFPSBackground`)
* Docs rebranded: `docs/GRAPHICS_ENHANCER.md`
* No vendor binaries redistributed — same runtime policy as upstream

### Credits

* Aeon SR engine: **Barbatos AWLS** — MIT
* ReShade: **crosire** — BSD 3-Clause
* DLSS / FidelityFX / XeSS: NVIDIA / AMD / Intel, under their own licences
* Fork, branding and the Chams menu: **hazu @ alexainc**
