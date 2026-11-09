/*
 * chams.hpp -- Chams feature (characters only) for the Free Fire emulator.
 *
 * created by hazu@alexainc
 *
 * CHARACTER DETECTION is locked on (no menu switch). It is the same test the
 * original DLL used:  mode == GL_TRIANGLES  AND  index count >= 1000  AND  the
 * current shader program declares the uniform "_CharaLightIntensity".
 *
 * THE EFFECTS (all independent, all with their own switch)
 * -------------------------------------------------------
 *   glow   = the whole 3D character filled with colour (highlight)
 *   stroke = ONE constant-width line on the OUTER side of the silhouette,
 *            drawn like a drawn outline around the character. It NEVER covers
 *            the body: the game's own texture stays visible inside it.
 *            Same two-colour scheme as the glow (and as v2.3, which is the
 *            version whose outline the user liked):
 *              through walls - visible where a wall / car / body is in front
 *              open part     - visible in plain sight
 *            The line is built the v2.3 way: a 0.0 / 1.0 depth mask over the
 *            silhouette, then shifted copies of the mesh. See chams_gl.inl.
 *   wire   = the classic 3D wireframe over the body (triangles as lines).
 *            Its own switch, size and colours, independent of the 2D stroke.
 *
 * TWO RENDERERS (menu: "Renderer")
 * ---------------------------------
 *   Direct  (v2.x/v3.0 behaviour): every character draw is repeated 1-9 times
 *           inside the game's own frame. Cheap, no buffers, but the result is
 *           depth-tested against the game's scene: a wall shader that writes a
 *           fake near depth, or a wall drawn after the character, can still
 *           hide it.
 *   Overlay (default, v3.2): the characters are drawn into our OWN small
 *           framebuffer (a mask), and once the frame is finished the mask is
 *           composited on top of the final image (outline dilation + fill).
 *           Nothing in the scene can hide it any more - not one wall, not five
 *           containers, not a wall skin, not the glue wall. It is a pure 2D
 *           post effect.
 *
 * DETECTION METHOD (menu: dropdown)
 * ---------------------------------
 *   0 strict    _CharaLightIntensity        (the original DLL's rule)
 *   1 wide      + _BackLightDir             (hair / cloth shaders)  [default]
 *   2 skinned   bone matrix uniforms        (catches unknown skin shaders)
 *   3 all       union of the three
 *
 * THROUGH WALLS is one switch, not a number
 * -----------------------------------------
 *   The throwable glue / ice wall is a translucent overlay that is drawn AFTER
 *   the characters, so its colour would sit on top of the chams. While that is
 *   the case, nothing but depth can help: the chams are simply made nearer in
 *   the depth buffer than the wall, so the wall stops painting over them.
 *   "Through walls" (default ON) does exactly that, automatically, for every
 *   character. There is no percentage to tune any more.
 *
 * Settings live in chams.ini / ReShade.ini, section [CHAMS].
 */

#pragma once

namespace reshade::chams
{
	// ---- character detection (locked, not exposed in the menu) ----------
	constexpr char k_character_uniform[] = "_CharaLightIntensity";
	// Meshes with fewer indices are skipped. This used to be 1000, which threw
	// away every small accessory of a character (hair strands, gloves, shoes,
	// hats, badges) - they stayed un-painted and looked like clothes covering
	// the chams. The shader uniform test below is the real filter, so this can
	// be tiny.
	constexpr int  k_min_index_count     = 64;
	constexpr int  k_max_draws_per_frame = 110;     // safety cap (meshes)
	constexpr int  k_toggle_key          = 0x77;    // VK_F8, in-game on/off
	constexpr float k_own_distance       = 4.0f;    // m: hide my own model (auto-refined)
	// Depth claim ("through walls"). The character is re-written into the depth
	// buffer inside this compressed slice of the depth range, so everything that
	// is drawn later - the translucent ice wall above all - loses the depth test
	// on those pixels and cannot paint over the chams.
	//   0.72 = strong (default): wins against the ice wall
	//   0.97 = gentle: only helps against a wall that is very close to the enemy
	// The old free percentage slider is gone on purpose - two numbers were
	// impossible to reason about in game.
	// The ice wall shader ("Ice_FakeDepth_Ground") deliberately writes a fake,
	// VERY near depth to hide everything behind it, so a partial claim is not
	// enough - it has to be the nearest value that exists (0.0) or the wall wins
	// on some frames and the enemy disappears ("sometimes it shows, sometimes
	// not"). 0.0 is deterministic: nothing drawn afterwards can cover the chams.
	constexpr float k_claim_near         = 0.0f;
	constexpr float k_claim_through      = 0.72f;   // kept for reference / dumps
	constexpr float k_claim_gentle       = 0.97f;

	struct settings
	{
		bool enabled = true;                        // master switch

		// ---- glow: how the character is painted -------------------------
		// Four modes, one switch in the menu ("Player highlight"):
		//   solid       whole body filled with the colour (the classic chams)
		//   blend       texture stays visible, the colour is blended over it
		//   cloth_tint  the REAL clothes are drawn (also through walls) and a
		//               light colour is laid over them - best of both
		//   cloth_only  no colour at all: the real clothes / skin / hair are
		//               drawn where they are hidden, nothing is tinted
		enum glow_mode_t { glow_solid = 0, glow_blend = 1, glow_cloth_tint = 2, glow_cloth_only = 3 };
		int   glow_mode               = glow_solid;
		bool  glow                    = true;
		float glow_through_color[3]   = { 1.00f, 0.25f, 0.05f }; // through walls
		bool  glow_open               = true;                    // open part
		float glow_open_color[3]      = { 1.00f, 0.85f, 0.15f };
		float glow_alpha              = 0.55f;                   // strength (0.9 washed the body out)

		// ---- stroke: 2D line around the silhouette (v2.3 algorithm) ------
		bool  stroke                  = true;
		int   stroke_open_size        = 4;                       // base px, open area (1..16)
		int   stroke_through_size     = 4;                       // base px, behind walls / glue wall (1..16)
		float stroke_through_color[3] = { 1.00f, 0.40f, 0.00f }; // through walls
		bool  stroke_open             = true;
		float stroke_open_color[3]    = { 1.00f, 0.95f, 0.35f }; // in plain sight
		float stroke_alpha            = 0.90f;                   // strength

		// ---- wireframe: classic 3D triangles-as-lines -------------------
		bool  wire                    = false;
		int   wire_size               = 1;                       // 1..8 px
		float wire_through_color[3]   = { 1.00f, 1.00f, 1.00f };  // through walls
		bool  wire_open               = true;
		float wire_open_color[3]      = { 0.90f, 0.95f, 1.00f };
		float wire_alpha              = 0.70f;

		// ---- third person / debug ---------------------------------------
		bool  hide_own                = true;   // no chams on my own character
		float own_distance            = k_own_distance;
		bool  dump                    = false;  // log character draws to %TEMP%
		bool  dump_all                = false;  // log EVERY draw (even filtered ones)
		// ---- renderer + detection ---------------------------------------
		// 0 = Overlay (mask framebuffer, composited on top at the end of the
		//     frame - nothing can hide it, not even the glue wall), 1 = Direct
		//     (v3.0 per-draw passes, depth tested by the game scene).
		// Direct is the default again: the overlay renderer needs a working
		// framebuffer + scissor handling on the emulator's driver, and a bad
		// composite means a black screen, which is not worth the risk by
		// default. Turn it on in the menu when you want the "always on top" look.
		int   render_mode             = 1;
		// 0 strict / 1 wide / 2 skinned / 3 all
		int   detect_mode             = 1;
		bool  wide_match              = true;   // kept: mode 1 == old wide match
		bool  through_walls           = true;   // walls / glue wall must not hide the chams
	};

	extern settings g_settings;
	extern bool     g_overlay_open;
	extern int      g_budget_left;
	extern unsigned g_frame_id;

	void dump_line(const char *text);   // append one line to %TEMP%\chams-dump.txt
	const char *dump_path();            // full path of the dump file (UTF-8)

	void begin_frame();      // called once per frame (wglSwapBuffers)
	void render_overlay();   // composites the player mask on top of the finished frame
	void poll_hotkey();      // F8: master switch on/off
	void load();
	void save();
}
