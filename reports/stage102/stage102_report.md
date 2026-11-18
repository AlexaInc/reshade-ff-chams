# Stage102 — Figma-Beautiful Collapsible Catalog

**Date:** 2026-11-18T23:22 UTC  
**Commit:** `fc0e525` (fix: restore s_headerBottomMap) on `chams-investigation-build` / `agent/chams-investigation-build`  
**Previous:** `2c33a57` stage102 design commit + `74bc458` stage101  
**CI:** `44419551442` (agent) SUCCESS, `44419556943`/`44419535755`+`44419545929` same SHA SUCCESS — 4× green, MSVC 64-bit 3m30s, artifact `ReShade64-chams` `14481602506` `2604111 B` `sha256:be57b1664c0df6eff70f6cdbfb900500c3bb9be220adbcef3a0240afe6763392`  
**Release:** [`stage102-chams-fc0e525`](https://github.com/AlexaInc/reshade-ff-chams/releases/tag/stage102-chams-fc0e525) — `ReShade64.dll` `5963776 B` `sha256:2962f6df6615334adb17b354e7cc155d21591f45e8cdd2d9dc153a8bb9b00249` (also stage101-chams-74bc458 live)

## What changed vs stage101 (user: "its not Collapsible and its not look like design")

| Figma | Stage101 | Stage102 |
|---|---|---|
| Page `#0B0E11` 836×3356 rx16, header `836×70 #11171D`, card `788×~170 rx8 #11171D` | `kBg 0.09/0.12/0.16` close but `#0F1419` docs | **Exact** `kPage #0B0E11`, `kHeaderBg #11171D`, `kCardBg #192128`, `kBorder #28323B`, `kMint #94E8C1`, `kText #EBF0F3`, `kTextDim #97A6B2` |
| Card header with `01 MASTER SWITCH…` mint number + chevron `>`/`v` | Non-collapsible `CatalogHeader` + `BeginChild` always open | **True collapsible** `BeginSectionV2(id,num,title,subtitle,toggle,forcedOpen)` — header is `InvisibleButton` full-width `headerH=GetFrameHeight()+10k(+14k subtitle)`, `g_catalog_open[id]` toggled on click (toggle hit-test excluded), chevron drawn via `Chevron()` dl lines, rounding `8k` top-only when open |
| Content area `#192128` with `10k` padding, border hairline | Mixed `kCardBg/kCardActive` child | **`BeginChild(id+"_body", ImVec2(w,0), true, AlwaysAutoResize)`** with `ChildBg=kCardBg`, `ChildRounding=8k`, `WindowPadding 10k/9k`, `ChildBorderSize=Hairline` — mirrors AeonSR `panel_widgets` `section()/band` pattern |
| Toggle 32×18 mint track, 6px knob | Previous mint dim toggle | **`AlexaToggle` 32×18** `AddRectFilled` rounding `r=y*0.5`, hover `Mix(bg,kText,0.06)`, off-border `kBorder`, knob 6px `0x080a0c` / `kTextMid` |
| Fill mode 2×2 grid selectable cards with mint radio dot | Single combo | **`FillModeCard`** `Button` + `AddCircleFilled` mint dot when selected, `FrameBorderSize=Hairline`, `FrameRounding=6k` |
| Search expands collapsed sections, "No matches" pink | Had search but collapsed logic flawed | **`SectionMatches` per section**, `forced = isSearch && matches`, `if(!matches) skip` + No matches pink `#F09CAD` banner |

## Palette (audited from Figma `svgviewer-output.svg` → `/tmp/figma.svg` `1.9M` `57×#EBF0F3 55×#97A6B2 45×#94E8C1 31×#28323B 29×#192128 17×#11171D 11×#0B0E11`)

```cpp
kPage      #0B0E11   page bg
kHeaderBg  #11171D   header/card header + Alexa header 58h
kCardBg    #192128   content child bg
kBorder    #28323B   hairline
kText      #EBF0F3   primary
kTextDim   #97A6B2   subtitle/dim
kTextMid   #687985   mid
kMint      #94E8C1   accent CTA/toggle/number
kMintDim   #203D34   fill selected
kPink      #F09CAD   no-match
kCardActive #1C2A31  hover mix
```

## AeonSR beautiful reference ported

`AeonSR-Graphics-Enhancer` (`9376cd5`) `panel.cpp:1503` subtitle `Graphics Enhancer by AlexaInc`, `panel_widgets.cpp:1366` `draw_logo` `g_cards[kMaxCards]` `palette()` `Theme` `section()/section_end()` `band_begin/band_end()` `chevron` `pill` `card_pad()` `lift` `AddRectFilled` — Stage102 mirrors via `BeginSectionV2`/`EndSectionV2` + `Mix`/`Hairline`/`CardRounding` and mint toggle matching `Theme.mint`.

## File

- `source/chams_menu.cpp` `483 lines` — `g_catalog_open` (master/render true, detect false default, glow/stroke true, wire/own/logs false, fps true), `g_search_buf[128]`, `ToLowerStr`/`ContainsI`/`SectionMatches`, `Unit`/`Col`/`Hairline`/`Mix`/`CardRounding`, `AlexaToggle`/`Chevron`/`FillModeCard`/`BeginSectionV2`/`EndSectionV2`/`DrawAlexaHeader` (58h `#11171D` logo 42×42, expand/collapse buttons, search `InputTextWithHint` framed `#192128` rounded 8k), 9 numbered sections 01–09 with per-panel `isSearch&&matches` forcedOpen, `draw_gui_chams()` `WindowBg=kPage` `Text=kText` `Border=kBorder`.
- `res/alexa_inc_logo.svg` `2705B`, `res/alexainc_logo.png` `30K` preserved.
- `.github/workflows/chams-64bit.yml` simple `checkout@v4`+`setup-msbuild@v2`+`msbuild …/ReShade.sln /t:ReShade /p:Configuration=Release /p:Platform=64-bit`+`upload-artifact bin/x64/Release/ReShade64.dll`.

## Build fix

- `s_headerBottomMap` undeclared `C2065` at `44418855663` — `BeginSectionV2` stored `headerH` bottom for gap logic; declaration removed accidentally with dead `BeginCatalogSection` cleanup. Restored `static std::unordered_map<std::string,float> s_headerBottomMap;` at top — 4× SUCCESS.

## Verification

- VM `ubuntu@132.226.189.63` synced `source/chams_menu.cpp` `483l` → `fc0e525` pushed both branches, triggered `44419551442` → `14481602506` OK, release `stage102-chams-fc0e525` live.
- Search: empty → all visible per `g_catalog_open` defaults; non-empty → `!matches` hidden, `forcedOpen=true` auto-expands; zero matches → pink banner.
- Toggle hit-test: header click toggles `open`, toggle rect `60k→32` excluded via `mp` check — no double-toggle.
- Child bg/border correct via `PushStyleColor(ChildBg)` + `BeginChild(..., AlwaysAutoResize)` — no manual height pre-draw needed (previous frame storage removed).
