# ReShade chams fork — build plan (GitHub Actions)

## 1. මොකද මේ පාර වෙනස්
කලින් builds (v3…v10) කළේ **proxy layer** එකක්: මගේ DLL එක ඉස්සරහින් තියලා
original ReShade එකට forward කරන එක. ඒකෙන් ප්‍රශ්න 3ක් ආවා (1114, menu නෑ, export
conflicts). දැන් ඔයා කිව්ව විදිහට **ReShade මූලාශ්‍රයෙන්ම build කරනවා** — proxy
නෑ, chain නෑ, rename trick නෑ:

```
BlueStacks → opengl32.dll  ← මේක build කරපු ReShade ම (chams mod එක ඇතුළේ)
                          → C:\Windows\System32\opengl32.dll  (ReShade තමන්ම forward කරනවා)
```

මේක තමයි "reshade clone කරලා UI එක වෙනස් කරන" එකේ නිවැරදි, buildable විදිහ.

## 2. Repo එකේ තියෙන වෙනස්කම් (patch එකේ)
| File | මොකද |
|---|---|
| `source/chams.hpp` / `chams.cpp` | settings struct + ReShade.ini එකේ `[CHAMS]` section |
| `source/chams_gl.inl` | ඇත්ත chams: draw call එක repeat කරන passes 2ක් |
| `source/chams_menu.cpp` | menu page එක — ReShade එකේම ImGui widgets |
| `source/opengl/opengl.cpp` | `glDrawArrays / glDrawElements / …Instanced / …BaseVertex` 6ක් wrap |
| `source/runtime_gui.cpp` | default pages අයින් කරලා අපේ page එක, menu key = **INSERT** |
| `source/runtime.hpp` | `draw_gui_chams()` declaration |
| `CMakeLists.txt`, `ReShade.vcxproj` | අලුත් files 4 build එකට |
| `.github/workflows/chams-64bit.yml` | Actions build (64-bit විතරක්) |
| `mod-tools/*` | `CLEAN-FIRST.bat`, `INSTALL.bat`, `UNINSTALL.bat`, README |

## 3. Effect එක වැඩ කරන විදිහ (ඇත්තම)
හැම 3D draw call එකක්ම ආයේ අඳිනවා:
1. **Highlight pass** — depth test off + depth write off + additive blend ⇒
   body එක බිත්තිය පිටිපස්සෙන් වුණත් පේනවා, සහ scene එකට වඩා දීප්තිමත්.
2. **Stroke pass** — `GL_POLYGON_MODE = GL_LINE`, line width = Size ⇒ triangle
   edge එකක් එකක්ම line එකක් වෙනවා = හැම corner/edge එකක් වටේම stroke.
ඊට පස්සේ **original draw එක එහෙමම** යනවා, GL state එක bit-by-bit
restore වෙනවා ⇒ සාමාන්‍ය frame එකට හානියක් නෑ.

සීමාව (ඇත්ත කියන්න ඕන): **ෆික්ස් color එකක් force කරන්න බෑ** — ඒකට game එකේ
shader replace කරන්න වෙනවා. ඒ නිසා "highlight" එක additive glow එකක් විදිහට
එනවා; stroke එක game එකේ color වලින්ම එනවා. Color force ඕන නම් කියන්න —
pipeline/shader replacement එක වෙනම වැඩක්.

`Min vertices` filter එකෙන් UI/text/sprite draws අල්ලන්නේ නෑ ⇒ menu එක
පිරිසිදුව තියෙනවා.

## 4. Build (ඔයා කරන්න ඕන දේ)
1. `github.com/crosire/reshade` → **Fork** (ඔයාගේ account එකට).
2. මම දෙන patch එක apply කරන්න (මම කරලා push කරන්නත් පුළුවන් — token එක එවන්න):
   ```
   git clone --recursive https://github.com/<you>/reshade
   cd reshade
   git apply /path/chams-fork.patch
   git add -A && git commit -m "chams menu + CI"
   git push
   ```
3. Push එකෙන් පස්සේ **Actions → chams-64bit → Run workflow** (උඩ push එකෙන්ම
   auto run වෙනවා).
4. Run එක ඉවර වුණාම **Artifacts → ReShade64-chams** → unzip → `ReShade64.dll`.

## 5. Install
1. `mod-tools\CLEAN-FIRST.bat` (admin) — පරණ layer files ඔක්කොම අයින් කරනවා
   (`opengl32_orig.dll`, `orig\`, `opengl32.ini/log`, `%LOCALAPPDATA%\opengl32*`).
2. `ReShade64.dll` එක `mod-tools\INSTALL.bat` එක තියෙන folder එකට දාලා
   **`INSTALL.bat`** (admin) — ඒක `opengl32.dll` විදිහට install කරනවා, කලින් තිබ්බ
   එක `opengl32_before_chams.dll` විදිහට තියාගන්නවා.
3. BlueStacks → Settings → Graphics → **Graphics engine = OPENGL** (අනිවාර්යයි).
4. Start → **INSERT** = chams menu.

Undo: `UNINSTALL.bat` (කලින් DLL එක ආපහු දානවා).

## 6. Build fail වුණොත්
Actions log එකේ error පේළිය එවන්න. වැඩිපුරම එන්න පුළුවන් දේවල්:
- `deps/*` submodule නෑ → `submodules: recursive` දාලා තියෙනවා, ඒත් fork එකේ
  submodules enable කරලා තියෙන්න ඕන,
- Python `glad` install fail → job එක ආයේ run කරන්න,
- chams files 4න් compile error → error text එක එවන්න, ඒක එක පේළියක fix.

## 6b. Character detection ගැන ඇත්ත
Layer/hook මට්ටමේ chams එකකට "මේ draw එක **character** එකක්" කියලා කියන්න
game එකේ shader/texture/bone දත්ත බලන්න වෙනවා. ඒක කරන්නේ නැතුව මම දුන්නේ:

- `Min vertices` — කුඩා UI/text/sprite draws ඉවත් කරනවා
- `Respect depth` toggle — wallhack එක on/off
- `Stroke size / passes` — කැපී පෙනෙන තරම

ඒ නිසා effect එක **3D geometry ඔක්කොම** වලට යනවා (character + props + walls),
ඒත් UI එකට යන්නේ නෑ. Character-only ඕන නම් heuristic 2ක් තියෙනවා (draw count /
උස ප්‍රමාණය / depth range), ඒත් ඒක game එකට වෙන වෙනම tune කරන්න වෙනවා — කැමති
නම් `chams_gl.inl` එකේ `draw_chams()` එකට ඒ filter එක දාන්න පුළුවන්; game එකේ
පින්තූරයක් එවන්න, එතකොට tune කරලා දෙන්නම්.

## 7. Push කරන්න මට ඕන දේ
- ඔයාගේ fork එකේ URL (`https://github.com/<you>/reshade`)
- Fine-grained PAT: `Contents: Read and write` (ඒ repo එකට විතරක්)
මම clone → patch → commit → **push** කරලා, Actions URL එකත් කියනවා.
Token එක එවන්නේ කැමති නම් විතරයි — නැත්නම් ඔයාට patch එක apply කරලා push
කරන්න පුළුවන්, ඒකට අවශ්‍ය විදිහ උඩ step 4 එකේ තියෙනවා.
