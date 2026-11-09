================================================================================
 ReShade chams fork -- how to build it and how to install it
================================================================================

PART 1 -- BUILD (GitHub Actions, no PC tools needed)
----------------------------------------------------
  The workflow is already in the repo:  .github/workflows/build.yml
  On every push to main it builds ReShade64.dll with MSVC on a Windows runner.

    Actions tab -> "Build ReShade (chams fork)" -> latest run
        -> Artifacts -> ReShade64-chams.zip
        -> unzip -> ReShade64.dll

  You can also press "Run workflow" to build without pushing a new commit.

PART 2 -- INSTALL (BlueStacks)
------------------------------
  1. CLEAN-FIRST.bat     (run as administrator)
        removes everything the earlier proxy builds left behind, so nothing
        can interfere. Your own files are not touched.
  2. Put ReShade64.dll next to HD-Player.exe and rename it to opengl32.dll
     -- or simply run INSTALL.bat with the DLL in the same folder as this file.
  3. BlueStacks -> Settings -> Graphics -> Graphics engine = OPENGL  (mandatory)
  4. Start BlueStacks, press INSERT.

  The fork needs no "original DLL" behind it: unlike the earlier layer builds,
  this IS ReShade (built from its own source), so it forwards to
  C:\Windows\System32\opengl32.dll by itself, exactly as upstream does.

PART 3 -- WHAT IS DIFFERENT FROM UPSTREAM ReShade
-------------------------------------------------
  source/chams.hpp            settings struct
  source/chams.cpp            [CHAMS] section of ReShade.ini
  source/chams_gl.inl         the two extra draw passes (highlight + stroke)
  source/chams_menu.cpp       the menu page (built with ReShade's own ImGui)
  modified:
    source/opengl/opengl.cpp    6 draw entry points wrapped
    source/runtime_gui.cpp      overlay pages replaced, INSERT as menu key
    source/runtime.hpp          declaration of the new page
    CMakeLists.txt              the new files added
    ReShade.vcxproj             same, for MSBuild users

  Everything else -- the effect runtime, the addon API, the D3D9/10/11/12,
  DXGI, Vulkan and OpenGL layers, input handling, screenshots -- is upstream
  and untouched.

PART 4 -- THE MENU
------------------
  INSERT opens ReShade's overlay (same fonts, same style, same input code).
  The pages are now:

     Chams      our page
       [x] Enable chams
       Body highlight
         [x] Body highlight (draw through walls)
         [x] Glow (additive blend)
         [ ] Respect depth (no wallhack)
      2D stroke
         [x] Stroke (wireframe outline)
             Stroke size (px)     0..12
             Stroke passes        1..4
       Filter
             Min vertices         0..1000
         [Reset to defaults]
     Add-ons    ReShade's own (only if addons are compiled in)
     Log        ReShade's diagnostics, useful for troubleshooting

  Settings are stored in the [CHAMS] section of ReShade.ini, next to the game.

PART 5 -- HOW THE EFFECT WORKS (so you can tune it)
---------------------------------------------------
  Every 3D draw call is repeated:
    pass 1  depth test off + depth writes off + additive blend
            -> the body is rasterised even behind walls and comes out brighter
    pass 2  polygon mode = LINE, line width = Stroke size
            -> every triangle edge is stroked, visible from any angle
  then the original draw happens unchanged, and the GL state is restored bit
  for bit, so the normal frame is not altered.

  Colours cannot be forced without replacing the game's shaders; additive
  blending is what produces the visible highlight. If you later want a fixed
  colour, the correct route is a shader/pipeline replacement, which is a much
  bigger change and has to be done per game API.

PART 6 -- IF THE BUILD FAILS
----------------------------
  Send the failing step's log. The three usual suspects:
    * submodules not checked out (fetch-depth / recursive) -- the workflow does
      this already,
    * Python cannot install glad (network) -- re-run the job,
    * a compiler error in one of the 4 chams files -- paste the error text, the
      fix is then a one-line change.
