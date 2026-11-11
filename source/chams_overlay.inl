/*
 * chams_overlay.inl -- the OVERLAY renderer (v3.2), created by hazu@alexainc
 *
 * Included at the end of chams_gl.inl, so every helper above is available.
 *
 * ===========================================================================
 * WHY THIS EXISTS
 * ===========================================================================
 * The "direct" renderer repeats every character draw inside the game's frame,
 * so its result is depth-tested against the game's own scene. That means:
 *    - a wall shader that writes a fake, very near depth hides the chams,
 *    - a wall drawn AFTER the character blends on top of them,
 *    - the glue wall, five containers, a wall skin, ... all win sometimes.
 * The only way to make it deterministic is to stop competing with the scene.
 *
 * HOW IT WORKS
 * ===========================================================================
 *   CAPTURE (inside the frame, per character mesh)
 *       the mesh is drawn into OUR OWN framebuffer. Colour writes are limited
 *       to the ALPHA channel and blending is additive, so the whole mask is a
 *       coverage map ("player pixels"), regardless of the model's textures,
 *       its colours or how dark the outfit is. Depth testing is off, so the
 *       mask is the union of the whole body - no z-fighting, no holes.
 *
 *   COMPOSITE (once, at the end of the frame, right before the swap)
 *       a fullscreen quad reads the mask and paints on top of the finished
 *       image:
 *           outline : the mask dilated by N pixels (8 offset passes with
 *                     GL_MAX blending -> a clean union, no double brightness)
 *           fill    : the mask itself, additive
 *       The interior is covered by the fill, so the outline survives only on
 *       the OUTSIDE of the silhouette - the same 2D look as the direct
 *       renderer, but now on top of the entire scene.
 *
 *   The final result does not depend on the depth buffer, the wall shaders,
 *   the draw order or the number of walls. That is the point.
 *
 * SAFETY
 * ===========================================================================
 *  - Nothing is deferred: the capture draws happen inside the game's frame,
 *    with the game's own vertex buffers, exactly like the direct renderer.
 *    (The v1.3 freeze came from re-playing draw calls after the frame.)
 *  - The mask framebuffer is created lazily and only when the overlay is the
 *    selected renderer. If creation or the shader fails, the code falls back
 *    to the direct renderer automatically - no black screen, no crash.
 *  - All GL state, the previous framebuffer and the viewport are saved and
 *    restored around every operation.
 */

#pragma once

// NOTE: this file is included from INSIDE namespace reshade::chams (see the
// end of chams_gl.inl), so it must not open the namespace again.

	// ---------------------------------------------------------------- GPU state
	struct overlay_gpu
	{
		bool   tried = false;      // creation was attempted
		bool   ready = false;      // ... and succeeded
		unsigned last_build_attempt = 0;   // frame id of last build attempt
		GLuint fbo = 0;
		GLuint mask_tex = 0;       // RGBA8, only the alpha channel is used
		GLuint program = 0;
		GLuint vao = 0;
		GLuint vbo = 0;
		GLint  loc_mask = -1, loc_offset = -1, loc_colour = -1;
		GLint  width = 0, height = 0;

		unsigned captured_frame = 0xFFFFFFFFu;   // frame the mask belongs to
		bool     has_mask = false;
	};

	inline overlay_gpu &ov() { static overlay_gpu g; return g; }

	// Both shaders are GLSL 130 - plain GL 3.0, which every driver the emulator
	// can run on supports. No extensions, no GL 4 syntax.
	static const char *const k_ov_vs = R"(#version 130
in vec2 a_pos;                 // 0..1 quad corner
uniform vec2 u_offset;         // shift in NDC units (outline dilation)
out vec2 v_uv;
void main()
{
	v_uv = a_pos;
	gl_Position = vec4(a_pos * 2.0 - 1.0 + u_offset, 0.0, 1.0);
}
)";

	static const char *const k_ov_fs = R"(#version 130
in vec2 v_uv;
uniform sampler2D u_mask;
uniform vec4 u_colour;         // rgb = colour, a = strength
out vec4 o_colour;
void main()
{
	// coverage 0..1 from the mask, then PREMULTIPLIED colour: with
	// premultiplied output the same shader works for additive blending
	// (GL_ONE, GL_ONE) and for the GL_MAX union of the outline passes.
	float m = clamp(texture(u_mask, v_uv).a, 0.0, 1.0) * u_colour.a;
	o_colour = vec4(u_colour.rgb * m, m);
}
)";

	// Preserve bindings as well as raster state: these functions run inside the
	// application's context, including during first-use allocation and resize.
	struct overlay_state_guard
	{
		GLint draw_fbo = 0, read_fbo = 0, program = 0, vao = 0, array_buffer = 0;
		GLint active_texture = GL_TEXTURE0, active_binding = 0, texture0_binding = 0;
		GLint vp[4] = {};
		GLfloat clear_color[4] = {};
		saved_state st;
		overlay_state_guard() : st(save_state())
		{
			gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
			gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
			gl.GetIntegerv(GL_CURRENT_PROGRAM, &program);
			gl.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
			gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
			gl.GetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
			gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &active_binding);
			gl.ActiveTexture(GL_TEXTURE0);
			gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &texture0_binding);
			gl.ActiveTexture(static_cast<GLenum>(active_texture));
			gl.GetIntegerv(GL_VIEWPORT, vp);
			gl.GetFloatv(GL_COLOR_CLEAR_VALUE, clear_color);
		}
		~overlay_state_guard()
		{
			gl.UseProgram(static_cast<GLuint>(program));
			gl.BindVertexArray(static_cast<GLuint>(vao));
			gl.BindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(array_buffer));
			gl.ActiveTexture(GL_TEXTURE0);
			gl.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture0_binding));
			gl.ActiveTexture(static_cast<GLenum>(active_texture));
			gl.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(active_binding));
			gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw_fbo));
			gl.BindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read_fbo));
			gl.Viewport(vp[0], vp[1], vp[2], vp[3]);
			gl.ClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
			restore_state(st);
		}
	};

	inline bool overlay_build()
	{
		overlay_gpu &g = ov();
		if (g.ready)
			return true;                 // already built successfully
		if (g.tried)
			return false;                // already tried and failed - do not retry
		// Shader compilation and framebuffer creation must happen only once,
		// on mode switch. Retrying every few frames causes repeated GPU stalls
		// during rendering which freezes the game frame. If the build fails,
		// Failed capture falls back to the direct per-draw path.
		g.tried = true;

		if (gl.GenFramebuffers == nullptr || gl.CreateShader == nullptr ||
		    gl.GenVertexArrays == nullptr || gl.BindVertexArray == nullptr)
		{
			reshade::log::message(reshade::log::level::warning,
				"[CHAMS] overlay renderer unavailable (old GL context) - using the direct renderer");
			return false;
		}

		overlay_state_guard guard;

		// ---- shader program -------------------------------------------------
		const auto compile = [](GLenum type, const char *src) -> GLuint {
			const GLuint s = gl.CreateShader(type);
			gl.ShaderSource(s, 1, &src, nullptr);
			gl.CompileShader(s);
			GLint ok = 0;
			gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
			if (!ok)
			{
				char log[512] = {};
				gl.GetShaderInfoLog(s, sizeof(log), nullptr, log);
				reshade::log::message(reshade::log::level::error,
					"[CHAMS] overlay shader failed: %s", log);
				gl.DeleteShader(s);
				return 0;
			}
			return s;
		};

		const GLuint vs = compile(GL_VERTEX_SHADER, k_ov_vs);
		const GLuint fs = compile(GL_FRAGMENT_SHADER, k_ov_fs);
		if (vs == 0 || fs == 0)
		{
			if (vs != 0) gl.DeleteShader(vs);
			if (fs != 0) gl.DeleteShader(fs);
			return false;
		}

		g.program = gl.CreateProgram();
		gl.AttachShader(g.program, vs);
		gl.AttachShader(g.program, fs);
		gl.BindAttribLocation(g.program, 0, "a_pos");
		gl.LinkProgram(g.program);
		gl.DeleteShader(vs);
		gl.DeleteShader(fs);

		GLint linked = 0;
		gl.GetProgramiv(g.program, GL_LINK_STATUS, &linked);
		if (!linked)
		{
			reshade::log::message(reshade::log::level::error,
				"[CHAMS] overlay program failed to link - using the direct renderer");
			gl.DeleteProgram(g.program);
			g.program = 0;
			return false;
		}

		g.loc_mask   = gl.GetUniformLocation(g.program, "u_mask");
		g.loc_offset = gl.GetUniformLocation(g.program, "u_offset");
		g.loc_colour = gl.GetUniformLocation(g.program, "u_colour");

		// ---- fullscreen quad ------------------------------------------------
		const GLfloat verts[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
		gl.GenVertexArrays(1, &g.vao);
		gl.BindVertexArray(g.vao);
		gl.GenBuffers(1, &g.vbo);
		gl.BindBuffer(GL_ARRAY_BUFFER, g.vbo);
		gl.BufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
		gl.EnableVertexAttribArray(0);
		gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);
		gl.BindVertexArray(0);
		gl.BindBuffer(GL_ARRAY_BUFFER, 0);

		// ---- mask framebuffer ----------------------------------------------
		gl.GenTextures(1, &g.mask_tex);
		gl.BindTexture(GL_TEXTURE_2D, g.mask_tex);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		gl.BindTexture(GL_TEXTURE_2D, 0);

		gl.GenFramebuffers(1, &g.fbo);

		g.ready = true;
		reshade::log::message(reshade::log::level::info,
			"[CHAMS] overlay renderer ready (mask framebuffer + composite shader)");
		return true;
	}

	// (re)allocate the mask texture when the viewport size changes
	inline bool overlay_ensure_size(GLint w, GLint h)
	{
		overlay_gpu &g = ov();
		if (!g.ready || w <= 0 || h <= 0)
			return false;
		if (g.width == w && g.height == h)
			return true;

		gl.BindTexture(GL_TEXTURE_2D, g.mask_tex);
		gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		gl.BindTexture(GL_TEXTURE_2D, 0);

		gl.BindFramebuffer(GL_FRAMEBUFFER, g.fbo);
		gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g.mask_tex, 0);
		const GLenum status = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
		gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			reshade::log::message(reshade::log::level::error,
				"[CHAMS] overlay framebuffer incomplete (0x%04X) - using the direct renderer", status);
			g.ready = false;
			return false;
		}

		g.width = w;
		g.height = h;
		// A newly allocated image has no valid contents, even if this frame
		// already captured meshes at a different viewport size.
		g.captured_frame = 0xFFFFFFFFu;
		g.has_mask = false;
		return true;
	}

	// ------------------------------------------------------------- mask capture
	// One call per character mesh, straight after the game's own draw.
	// Colour writes go to the alpha channel only and blending is additive, so
	// the mask ends up as coverage ("this pixel is a player"), independent of
	// the model's textures.
	template <typename F>
	inline void overlay_capture_mesh(F &&real_draw)
	{
		overlay_gpu &g = ov();
		if (!g.ready)
			return;

		GLint vp[4] = { 0, 0, 0, 0 };
		gl.GetIntegerv(GL_VIEWPORT, vp);
		if (vp[2] <= 0 || vp[3] <= 0)
			return;
		overlay_state_guard guard;
		if (!overlay_ensure_size(vp[2], vp[3]))
			return;


		const bool first_this_frame = (g.captured_frame != g_frame_id);

		gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, g.fbo);
		gl.Viewport(0, 0, vp[2], vp[3]);

		// IMPORTANT: the emulator uses a scissor rectangle for its UI, and a
		// scissor left enabled would clip our clear AND our draws, so the mask
		// would keep stale pixels from earlier frames (that is what turned the
		// whole screen black: a stale mask made the stroke pass darken
		// everything). Scissor is therefore always off while we touch the mask.
		gl.Disable(GL_SCISSOR_TEST);

		if (first_this_frame)
		{
			gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			gl.Clear(GL_COLOR_BUFFER_BIT);
			g.captured_frame = g_frame_id;
			g.has_mask = true;
		}

		gl.Disable(GL_DEPTH_TEST);
		gl.Disable(GL_CULL_FACE);
		gl.Enable(GL_BLEND);
		gl.BlendEquation(GL_FUNC_ADD);
		gl.BlendFunc(GL_ONE, GL_ONE);                 // accumulate coverage
		gl.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);   // alpha only
		gl.DepthMask(GL_FALSE);
		gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);

		real_draw();

	}

	// ---------------------------------------------------------------- composite
	// Called once per frame, right before the swap, with the game's final image
	// already in the framebuffer.
	void render_overlay()   // declared in chams.hpp, called from wglSwapBuffers
	{
		overlay_gpu &g = ov();

		if (g_opengl_context == nullptr || g_settings.render_mode != 0 || !g_settings.enabled || g_overlay_open)
			return;
		// overlay_build() must NOT be called here (during wglSwapBuffers) because
		// it may compile shaders, which stalls the GPU pipeline and freezes the
		// game frame. Pre-building happens in draw_chams() when switching to
		// overlay mode. If not ready yet, skip this frame.
		// An empty frame is normal (e.g. lobby transition), not a renderer failure.
		if (!g.tried || !g.ready || !g.has_mask || g.captured_frame != g_frame_id)
			return;

		const settings &s = g_settings;
		const bool want_glow   = s.glow;
		const bool want_stroke = s.stroke;
		if (!want_glow && !want_stroke)
			return;

		overlay_state_guard guard;

		gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(guard.draw_fbo));
		gl.Viewport(0, 0, g.width, g.height);

		gl.Disable(GL_DEPTH_TEST);
		gl.Disable(GL_CULL_FACE);
		gl.Disable(GL_SCISSOR_TEST);          // composite the WHOLE frame
		gl.DepthMask(GL_FALSE);
		gl.Disable(GL_POLYGON_OFFSET_FILL);
		gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

		gl.UseProgram(g.program);
		gl.ActiveTexture(GL_TEXTURE0);
		gl.BindTexture(GL_TEXTURE_2D, g.mask_tex);
		if (g.loc_mask != -1)
			gl.Uniform1i(g.loc_mask, 0);
		gl.BindVertexArray(g.vao);

		const auto quad = [&](float ox, float oy, const float colour[3], float alpha) {
			if (g.loc_offset != -1) gl.Uniform2f(g.loc_offset, ox, oy);
			if (g.loc_colour != -1) gl.Uniform4f(g.loc_colour, colour[0], colour[1], colour[2], alpha);
			gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		};

		// outline -----------------------------------------------------------------
		// 8 shifted copies, combined with GL_MAX so overlapping copies do not add
		// up to a brighter line (that is what a "MAX" blend is for).
		if (want_stroke && s.stroke_alpha > 0.0f && s.stroke_open_size > 0)
		{
			const int   band = s.stroke_open_size;
			const float step_x = 2.0f * static_cast<float>(band) / static_cast<float>(g.width);
			const float step_y = 2.0f * static_cast<float>(band) / static_cast<float>(g.height);
			const float diag   = 0.7071f;

			gl.Enable(GL_BLEND);
			gl.BlendEquation(GL_MAX);
			gl.BlendFunc(GL_ONE, GL_ONE);

			const float ox[8] = { 1, -1, 0, 0, diag, diag, -diag, -diag };
			const float oy[8] = { 0, 0, 1, -1, diag, -diag, diag, -diag };
			for (int i = 0; i < 8; ++i)
				quad(ox[i] * step_x, oy[i] * step_y, s.stroke_open_color, s.stroke_alpha);

			gl.BlendEquation(GL_FUNC_ADD);
		}

		// glow fill ---------------------------------------------------------------
		// additive: the body lights up with the highlight colour, the drawn line
		// around it still shows on the outside because the fill only covers the
		// silhouette itself.
		if (want_glow && s.glow_alpha > 0.0f)
		{
			gl.BlendFunc(GL_ONE, GL_ONE);          // premultiplied additive
			quad(0.0f, 0.0f, s.glow_open_color, s.glow_alpha);
		}
		else if (want_stroke)
		{
			// stroke only: erase the inside of the dilated line so what is left is a
			// clean 2D outline instead of a filled silhouette.
			// This is the ONLY pass that darkens the image, and it only darkens where
			// the mask is 1 (dest *= 1 - mask*alpha), so it can never black the
			// screen unless the mask itself is wrong - which is why scissor and the
			// clear above matter.
			gl.BlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
			quad(0.0f, 0.0f, s.stroke_open_color, 0.85f);
		}
	}
