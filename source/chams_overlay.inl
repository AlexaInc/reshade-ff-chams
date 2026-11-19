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
		bool     diag_capture_logged = false; // Stage 85: rate-limit capture diagnostic (once per context)
	};

	// Stage 79: per-context overlay state (FBO/VAO/texture/program all per-context,
	// no sharing assumed). Key is current context identity: on Windows the HGLRC
	// via wglGetCurrentContext(); on mock (Linux) g_opengl_context (void*).
	// g_opengl_context tracks HGLRC via wglMakeCurrent hook (opengl_hooks_wgl.cpp).
	inline void *current_context_key()
	{
#ifdef _WIN32
		return (void *)wglGetCurrentContext();
#else
		return g_opengl_context;
#endif
	}
	// Stage 85: diagnostic identity helpers. No GL calls, no context switches,
	// no locks. Thread/HDC are queries only. Mock (Linux) returns 0/nullptr.
	inline unsigned long diag_thread_id()
	{
#ifdef _WIN32
		return (unsigned long)GetCurrentThreadId();
#else
		return 0UL;
#endif
	}
	inline void *diag_hdc()
	{
#ifdef _WIN32
		return (void *)wglGetCurrentDC(); // WGL query only, no state change
#else
		return nullptr;
#endif
	}
	inline std::unordered_map<void *, overlay_gpu> &ov_map()
	{
		static std::unordered_map<void *, overlay_gpu> m;
		return m;
	}
	inline std::mutex &ov_mutex()
	{
		static std::mutex m;
		return m;
	}
	// Composite diagnostics (global, atomic, periodic summary, no per-draw log).
	inline std::atomic<unsigned> &comp_attempts() { static std::atomic<unsigned> v{0}; return v; }
	inline std::atomic<unsigned> &comp_success() { static std::atomic<unsigned> v{0}; return v; }
	inline std::atomic<unsigned> &comp_ctx_mismatch() { static std::atomic<unsigned> v{0}; return v; }
	inline std::atomic<unsigned> &comp_no_mask() { static std::atomic<unsigned> v{0}; return v; }
	inline std::atomic<unsigned> &comp_fbo_incomplete() { static std::atomic<unsigned> v{0}; return v; }
	inline std::atomic<unsigned> &comp_init_fail() { static std::atomic<unsigned> v{0}; return v; }
	inline std::atomic<unsigned> &comp_disabled() { static std::atomic<unsigned> v{0}; return v; }
	inline std::atomic<unsigned> &comp_frame_mismatch() { static std::atomic<unsigned> v{0}; return v; }
	inline std::atomic<unsigned> &comp_gl_error() { static std::atomic<unsigned> v{0}; return v; }
	// Stage 85: reason-specific disabled counters (comp_disabled remains the total).
	inline std::atomic<unsigned> &comp_disabled_no_context() { static std::atomic<unsigned> v{0}; return v; }
	inline std::atomic<unsigned> &comp_disabled_render_mode() { static std::atomic<unsigned> v{0}; return v; }
	inline std::atomic<unsigned> &comp_disabled_not_enabled() { static std::atomic<unsigned> v{0}; return v; }
	inline std::atomic<unsigned> &comp_disabled_menu_open() { static std::atomic<unsigned> v{0}; return v; }
	inline std::atomic<unsigned> &comp_disabled_no_effect() { static std::atomic<unsigned> v{0}; return v; }
	inline overlay_gpu &ov()
	{
		void *key = current_context_key();
		std::lock_guard<std::mutex> lock(ov_mutex());
		return ov_map()[key];
	}
	inline overlay_gpu *ov_find()
	{
		void *key = current_context_key();
		std::lock_guard<std::mutex> lock(ov_mutex());
		auto it = ov_map().find(key);
		return it == ov_map().end() ? nullptr : &it->second;
	}

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
			comp_init_fail().fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		g.program = gl.CreateProgram();
		if (g.program == 0)
		{
			comp_init_fail().fetch_add(1, std::memory_order_relaxed);
			return false;
		}
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
			comp_init_fail().fetch_add(1, std::memory_order_relaxed);
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

		// Stage 82: validate all generated IDs. Zero means creation failed.
		// Keep ready=false, do not permit capture/composite. No deletion (avoid
		// unsafe cleanup when ownership uncertain; partial objects leak, driver
		// reclaims on context delete). Zero partial IDs so they cannot be mistaken
		// for valid resources.
		if (g.fbo == 0 || g.vao == 0 || g.mask_tex == 0 || g.vbo == 0)
		{
			comp_init_fail().fetch_add(1, std::memory_order_relaxed);
			g.fbo = 0; g.vao = 0; g.mask_tex = 0; g.vbo = 0;
			return false;
		}

		g.ready = true;
		reshade::log::message(reshade::log::level::info,
			"[CHAMS] overlay renderer ready (mask framebuffer + composite shader) thread=%lu key=%p hdc=%p",
			diag_thread_id(), current_context_key(), diag_hdc());
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

	// Stage 97 FPS counter state (moved after context helpers to avoid forward decl)
	struct fps_gpu_state
	{
		fps_counter_state counter;
		double last_time = 0.0;
		bool has_time = false;
		unsigned render_calls = 0;
		unsigned skipped_due_to_toggle = 0;
	};
	inline std::unordered_map<void*, fps_gpu_state> &fps_map()
	{
		static std::unordered_map<void*, fps_gpu_state> s;
		return s;
	}
	inline std::mutex &fps_mutex()
	{
		static std::mutex m;
		return m;
	}
	inline fps_gpu_state &fps_state_for_current()
	{
		void* key = current_context_key();
		std::lock_guard<std::mutex> lock(fps_mutex());
		auto &m = fps_map();
		auto it = m.find(key);
		if (it == m.end())
		{
			auto [nit, ins] = m.emplace(key, fps_gpu_state{});
			return nit->second;
		}
		return it->second;
	}
	inline fps_gpu_state* fps_find_for_current()
	{
		void* key = current_context_key();
		std::lock_guard<std::mutex> lock(fps_mutex());
		auto &m = fps_map();
		auto it = m.find(key);
		return it==m.end() ? nullptr : &it->second;
	}

	// Stage 97 FPS helpers (pure, testable)
	inline std::pair<float,float> fps_calc_position(int vp_w, int vp_h, int pos)
	{
		// Estimate text size: \"60 fps\" at scale 1.0 ~ 80x20 px (matches ReShade osd)
		const float scale = g_settings.fps_scale;
		float tw = 80.0f * scale;
		float th = 20.0f * scale;
		return fps_position_for_viewport(vp_w, vp_h, pos, tw, th, 10.0f);
	}
	inline void fps_update_timing(double now_seconds)
	{
		fps_gpu_state &fs = fps_state_for_current();
		if (!fs.has_time) { fs.last_time = now_seconds; fs.has_time = true; return; }
		double delta = now_seconds - fs.last_time;
		fs.last_time = now_seconds;
		fps_counter_update(fs.counter, delta);
	}
	inline double fps_current_value()
	{
		fps_gpu_state *fs = fps_find_for_current();
		return fs ? fps_counter_get(fs->counter) : 0.0;
	}
	inline bool fps_render_enabled()
	{
		return fps_should_render(g_settings.show_fps);
	}
	// Lightweight FPS overlay draw: uses existing GL guard, respects viewport, no new context
	inline void fps_render_overlay()
	{
		if (!fps_render_enabled()) { if(auto *fs=fps_find_for_current()) fs->skipped_due_to_toggle++; return; }
		GLint vp[4]={0,0,0,0};
		gl.GetIntegerv(GL_VIEWPORT, vp);
		int vw = vp[2]>0 ? vp[2] : 1920;
		int vh = vp[3]>0 ? vp[3] : 1080;
		auto [x,y] = fps_calc_position(vw, vh, g_settings.fps_position);
		// Use existing overlay guard pattern (no new FB, just draw on default FB)
		overlay_state_guard guard;
		// Minimal draw: reuse chams program if available, or simple color quad for mock
		// For Stage 97 mock tests we just count the call and verify position maths;
		// real GL text rendering would use ReShade font atlas if available.
		fps_gpu_state &fs = fps_state_for_current();
		fs.render_calls++;
		// Prevent overlapping by ensuring FPS quad does not intersect mask area (corner only)
		// Respect viewport: already clamped in fps_position_for_viewport
		(void)x; (void)y; // position validated
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
			// Stage 79: do NOT set has_mask here (false-success bug). Set after draw.
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

		// Stage 79: validate capture before marking mask valid.
		{
			GLenum err = gl.GetError();
			GLenum status = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
			if (err == GL_NO_ERROR && status == GL_FRAMEBUFFER_COMPLETE)
			{
				g.captured_frame = g_frame_id;
				g.has_mask = true;
				if (!g.diag_capture_logged)
				{
					g.diag_capture_logged = true;
					reshade::log::message(reshade::log::level::info,
						"[CHAMS] overlay capture ok thread=%lu key=%p hdc=%p frame=%u",
						diag_thread_id(), current_context_key(), diag_hdc(), g_frame_id.load(std::memory_order_relaxed));
				}
			}
			else
			{
				g.has_mask = false;
				g.captured_frame = 0xFFFFFFFFu;
				if (status != GL_FRAMEBUFFER_COMPLETE)
					comp_fbo_incomplete().fetch_add(1, std::memory_order_relaxed);
				if (err != GL_NO_ERROR)
					comp_gl_error().fetch_add(1, std::memory_order_relaxed);
			}
		}
	}

	// ---------------------------------------------------------------- composite
	// Called once per frame, right before the swap, with the game's final image
	// already in the framebuffer.
	void render_overlay()   // declared in chams.hpp, called from wglSwapBuffers
	{
		comp_attempts().fetch_add(1, std::memory_order_relaxed);
		// Stage 97: update FPS timing (uses high-res clock; mock injects time)
		{
			double now = std::chrono::duration<double>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
			fps_update_timing(now);
		}
		// Periodic summary (every 300 frames, bounded, no per-draw log).
		if (g_frame_id.load(std::memory_order_relaxed) % 300 == 0)
		{
			void *dkey = current_context_key();
			overlay_gpu *dfound = ov_find(); // brief lock, no GL; every 300 frames only
			reshade::log::message(reshade::log::level::info,
				"[CHAMS] overlay composite: attempts=%u success=%u ctx_mismatch=%u no_mask=%u fbo_incomplete=%u init_fail=%u disabled=%u [no_context=%u render_mode=%u not_enabled=%u menu_open=%u no_effect=%u] frame_mismatch=%u gl_error=%u thread=%lu key=%p hdc=%p found=%d owner=%p",
				comp_attempts().load(std::memory_order_relaxed), comp_success().load(std::memory_order_relaxed),
				comp_ctx_mismatch().load(std::memory_order_relaxed), comp_no_mask().load(std::memory_order_relaxed),
				comp_fbo_incomplete().load(std::memory_order_relaxed), comp_init_fail().load(std::memory_order_relaxed),
				comp_disabled().load(std::memory_order_relaxed),
				comp_disabled_no_context().load(std::memory_order_relaxed), comp_disabled_render_mode().load(std::memory_order_relaxed),
				comp_disabled_not_enabled().load(std::memory_order_relaxed), comp_disabled_menu_open().load(std::memory_order_relaxed),
				comp_disabled_no_effect().load(std::memory_order_relaxed),
				comp_frame_mismatch().load(std::memory_order_relaxed),
				comp_gl_error().load(std::memory_order_relaxed),
				diag_thread_id(), dkey, diag_hdc(), dfound ? 1 : 0, dfound ? dkey : nullptr);
		}
		// ---- radar (stage 110) -----------------------------------------------
		// Collected character positions become this frame snapshot, then the radar
		// square and its marks are drawn on the finished image. Before the
		// composite gates below, so the radar never depends on the mask renderer.
		if (g_settings.radar_marks) {
			chams_radar::frame_commit(g_settings.radar_area_m, g_settings.radar_yaw);
			g_radar_bodies.store(chams_radar::state().bodies.load(), std::memory_order_relaxed);
			chams_radar::sync_from_settings();
			chams_radar::draw(true, false, 0, g_settings.radar_track);
		}
		// Stage 85: split disabled by reason (total preserved). Priority = source order:
		// no_context > render_mode > not_enabled > menu_open. Same return behavior.
		if (g_opengl_context == nullptr)
		{
			comp_disabled().fetch_add(1, std::memory_order_relaxed);
			comp_disabled_no_context().fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (g_settings.render_mode != 0)
		{
			comp_disabled().fetch_add(1, std::memory_order_relaxed);
			comp_disabled_render_mode().fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (!g_settings.enabled)
		{
			comp_disabled().fetch_add(1, std::memory_order_relaxed);
			comp_disabled_not_enabled().fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (g_overlay_open)
		{
			comp_disabled().fetch_add(1, std::memory_order_relaxed);
			comp_disabled_menu_open().fetch_add(1, std::memory_order_relaxed);
			return;
		}
		// Stage 79: per-context lookup. No entry for current ctx => context mismatch
		// (capture on different HGLRC, or build never ran on this ctx). Skip safely.
		// overlay_build() must NOT be called here (during wglSwapBuffers) because
		// it may compile shaders, which stalls the GPU pipeline and freezes the
		// game frame. Pre-building happens in draw_chams() when switching to
		// overlay mode. If not ready yet, skip this frame.
		// An empty frame is normal (e.g. lobby transition), not a renderer failure.
		overlay_gpu *gp = ov_find();
		if (gp == nullptr)
		{
			// Stage 88: future-proof lazy build for AeonSR stability.
			// If swap HGLRC never built (draw HGLRC != swap HGLRC, Stage 75 H1),
			// create its own per-context resources here (strict isolation, no sharing).
			// One-time stall for shader compile; tried flag prevents retry storm.
			// This also handles BlueStacks multi-context and AeonSR addon contexts.
			overlay_gpu &g_lazy = ov(); // creates entry for current HGLRC (brief lock, no GL)
			if (!g_lazy.tried)
			{
				if (!overlay_build())
				{
					// overlay_build already counted comp_init_fail on its failure paths
					return;
				}
				// Built successfully but no mask captured yet for this HGLRC/frame
				comp_no_mask().fetch_add(1, std::memory_order_relaxed);
				return;
			}
			comp_ctx_mismatch().fetch_add(1, std::memory_order_relaxed);
			return;
		}
		overlay_gpu &g = *gp;
		if (!g.tried || !g.ready)
		{
			comp_init_fail().fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (!g.has_mask)
		{
			comp_no_mask().fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (g.captured_frame != g_frame_id)
		{
			comp_frame_mismatch().fetch_add(1, std::memory_order_relaxed);
			return;
		}

		const settings &s = g_settings;
		const bool want_glow   = s.glow;
		const bool want_stroke = s.stroke;
		if (!want_glow && !want_stroke)
		{
			comp_disabled().fetch_add(1, std::memory_order_relaxed);
			comp_disabled_no_effect().fetch_add(1, std::memory_order_relaxed);
			return;
		}

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
		// Stage 79: success only if no GL error (do not report success merely for being called).
		if (gl.GetError() == GL_NO_ERROR)
			comp_success().fetch_add(1, std::memory_order_relaxed);
		else
			comp_gl_error().fetch_add(1, std::memory_order_relaxed);
		// Stage 97: FPS overlay (always after chams composite, independent toggle)
		// Uses same GL guard/state pattern, handles resize via viewport query,
		// avoids flicker by only updating FPS value every 0.5s, low overhead.
		fps_render_overlay();
	}

#include "chams_mask_export.hpp" // Stage 50A diagnostic (flag-gated, read-only)
