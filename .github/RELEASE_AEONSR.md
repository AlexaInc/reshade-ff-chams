# AlexaInc Enhancer v1.0.0

ReShade add-on that brings **DLSS / FSR / XeSS temporal anti-aliasing, frame generation
and DLSS neural rendering** to games that never shipped them.

Fork of [Aeon SR](https://github.com/BarbatosAWLS/AeonSR) by **Barbatos AWLS** (MIT),
rebranded and extended by **AlexaInc**. Engine copyright stays with the original author;
the AlexaInc name, logo and packaging are the fork's.

---

## Which download do I need?

| Package | Contents | Use it when |
| --- | --- | --- |
| **`AlexaIncEnhancer-v1.0.0-plugins.zip`** | `AlexaIncEnhancer.addon64`, `AlexaIncEnhancer.addonfx` | You already have the vendor DLLs (DLSS / FSR / XeSS) beside your game, or you only want the upscaler that ships inside the add-on |
| **`AlexaIncEnhancer-v1.0.0-dlls.zip`** | every vendor runtime DLL only | You just need to drop the runtimes next to a game that already has the add-on |
| **`AlexaIncEnhancer-v1.0.0-full.zip`** | add-on + effect file + **all DLLs** + logo + this readme | First install. Unzip, copy everything into `reshade-shaders\Addons` (and the DLLs into the game folder if the game needs them there) |

A bare `AlexaIncEnhancer.addon64` is attached to the release as well, for updating
without touching your DLLs.

### DLLs inside `-dlls.zip` and `-full.zip`

| DLL | Provides | Vendor |
| --- | --- | --- |
| `nvngx_dlss.dll` | DLSS Super Resolution | NVIDIA |
| `nvngx_dlssg.dll` | DLSS Frame Generation | NVIDIA |
| `nvngx_dlssnr.dll` | **DLSS Neural Rendering** (310.8 SF v2) | NVIDIA |
| `libxess.dll` | XeSS (DX12) | Intel |
| `libxess_dx11.dll` | XeSS (DX11) | Intel |
| `libxess_fg.dll` | XeSS Frame Generation | Intel |
| `libxell.dll` | XeLL low-latency | Intel |
| `amd_fidelityfx_upscaler_dx12.dll` | FSR 3.1 / FSR 4 upscaling | AMD |
| `amd_fidelityfx_framegeneration_dx12.dll` | FSR frame generation | AMD |
| `amd_fidelityfx_loader_dx12.dll` | FidelityFX loader used by the two above | AMD |

Every one of these is **proprietary vendor software**, included under the vendor's own
licence (see `THIRD-PARTY-NOTICES.md` in the repo). They are **not** part of the MIT
licensed Aeon SR engine, and they are not modified here.

---

## Install

1. Install **ReShade with add-on support** for the game.
2. Copy `AlexaIncEnhancer.addon64` and `AlexaIncEnhancer.addonfx` into
   `…\reshade-shaders\Addons\` next to the game executable.
3. If you use the full package, copy the vendor DLLs to the folder the add-on expects —
   the simplest is `…\reshade-shaders\Addons\` together with the add-on; the add-on also
   looks beside the game executable.
4. Start the game, open the ReShade overlay (**Home** by default), go to **Add-ons**.
   The panel is called **AlexaInc Enhancer** and its overlay shows the AlexaInc logo.

## What this build is

* Same Aeon SR 1.3.2 engine, renamed and rebranded to **AlexaInc Enhancer v1.0.0**
* IP geolocation of the engine unchanged — if a game does not work with upstream Aeon SR,
  it will not work here either
* FPS counter on by default
* **FPS counter background**: solid or frosted plate, colour, opacity, corner radius and
  padding (`[OVERLAY] FPSBackground`, `[STYLE] ColFPSBackground`)
* Logo: `AlexaInc.svg` / `AlexaInc.ico` / `alexainc_logo.png` in this package

## Credits

* Aeon SR engine — **Barbatos AWLS**, MIT
* ReShade — **crosire**, BSD 3-Clause
* DLSS, FidelityFX, XeSS — NVIDIA / AMD / Intel
* Rebrand, packaging, Chams menu — **hazu @ alexainc**
