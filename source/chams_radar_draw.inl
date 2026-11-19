/*
 * chams_radar_draw.inl -- draws the player-centred radar square.
 * created by hazu@alexainc
 *
 * Included from chams_gl.inl AFTER chams_overlay.inl. Runs in the same
 * wglSwapBuffers window as the mask composite, on the game's final image.
 *
 * OWN GL PROGRAM (stage 111)
 * --------------------------
 * This used to reuse the mask-composite program from chams_overlay.inl, which is
 * only built in Overlay render mode. In Direct mode (the default) that program
 * does not exist, so the radar drew nothing at all -- the "always 0 players,
 * nothing on screen" report. It now owns a tiny GL 3.0 program and a quad buffer,
 * built once per context on first use, so it is independent of the renderer mode
 * and of the mask pipeline.
 */
#pragma once

namespace chams_radar
{
	// One screen-space quad, in pixels, from the GL viewport's top left corner.
	struct px_quad { float x0, y0, x1, y1; };

	struct radar_gpu
	{
		GLuint program = 0, vao = 0, vbo = 0;
		GLint  loc_rect = -1, loc_colour = -1;
		bool   tried = false, ready = false;
	};
	inline std::unordered_map<void *, radar_gpu> &gpu_map()
	{
		static std::unordered_map<void *, radar_gpu> m;
		return m;
	}
	inline std::mutex &gpu_mutex()
	{
		static std::mutex m;
		return m;
	}
	inline void *gpu_key()
	{
		return reinterpret_cast<void *>(static_cast<uintptr_t>(
			wglGetCurrentContext() != nullptr ? reinterpret_cast<uintptr_t>(wglGetCurrentContext()) : 0u));
	}
	inline radar_gpu &gpu()
	{
		void *k = gpu_key();
		std::lock_guard<std::mutex> lock(gpu_mutex());
		return gpu_map()[k];
	}

	static const char *const k_radar_vs = R"(#version 130
in vec2 a_pos;            // unit quad 0..1
uniform vec4 u_rect;      // xy = top left in NDC, zw = size in NDC
void main()
{
	vec2 p = u_rect.xy + a_pos * u_rect.zw;
	gl_Position = vec4(p, 0.0, 1.0);
}
)";
	static const char *const k_radar_fs = R"(#version 130
uniform vec4 u_colour;
out vec4 o_colour;
void main() { o_colour = u_colour; }
)";

	inline bool build()
	{
		radar_gpu &g = gpu();
		if (g.ready)
			return true;
		if (g.tried)
			return false;
		g.tried = true;
		// Full entry-point check: a missing VAO/BufferData entry point used to be a
		// crash, and a crash inside wglSwapBuffers is a black screen.
		if (gl.CreateShader == nullptr || gl.CreateProgram == nullptr ||
			gl.DrawArrays == nullptr || gl.GenVertexArrays == nullptr ||
			gl.BindVertexArray == nullptr || gl.GenBuffers == nullptr ||
			gl.BufferData == nullptr || gl.VertexAttribPointer == nullptr ||
			gl.EnableVertexAttribArray == nullptr || gl.GetShaderiv == nullptr ||
			gl.GetProgramiv == nullptr || gl.GetUniformLocation == nullptr ||
			gl.Uniform4f == nullptr || gl.UseProgram == nullptr)
		{
			reshade::log::message(reshade::log::level::warning,
				"[CHAMS] radar disabled: GL entry points missing on this context");
			return false;
		}

		const auto compile = [](GLenum type, const char *src) -> GLuint {
			const GLuint s = gl.CreateShader(type);
			gl.ShaderSource(s, 1, &src, nullptr);
			gl.CompileShader(s);
			GLint ok = 0;
			gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
			if (!ok) {
				char log[512] = {};
				gl.GetShaderInfoLog(s, sizeof(log), nullptr, log);
				reshade::log::message(reshade::log::level::error, "[CHAMS] radar shader failed: %s", log);
				gl.DeleteShader(s);
				return 0;
			}
			return s;
		};
		const GLuint vs = compile(GL_VERTEX_SHADER, k_radar_vs);
		const GLuint fs = compile(GL_FRAGMENT_SHADER, k_radar_fs);
		if (vs == 0 || fs == 0)
			return false;

		g.program = gl.CreateProgram();
		gl.AttachShader(g.program, vs);
		gl.AttachShader(g.program, fs);
		gl.BindAttribLocation(g.program, 0, "a_pos");
		gl.LinkProgram(g.program);
		gl.DeleteShader(vs);
		gl.DeleteShader(fs);
		GLint linked = 0;
		gl.GetProgramiv(g.program, GL_LINK_STATUS, &linked);
		if (!linked) {
			reshade::log::message(reshade::log::level::error, "[CHAMS] radar program failed to link");
			gl.DeleteProgram(g.program);
			g.program = 0;
			return false;
		}
		g.loc_rect = gl.GetUniformLocation(g.program, "u_rect");
		g.loc_colour = gl.GetUniformLocation(g.program, "u_colour");

		const GLfloat verts[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
		gl.GenVertexArrays(1, &g.vao);
		gl.BindVertexArray(g.vao);
		gl.GenBuffers(1, &g.vbo);
		gl.BindBuffer(GL_ARRAY_BUFFER, g.vbo);
		gl.BufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
		gl.EnableVertexAttribArray(0);
		gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
		gl.BindVertexArray(0);
		g.ready = true;
		return true;
	}

	inline void push_quad(std::vector<px_quad> &out, float cx, float cy, float hw, float hh)
	{
		out.push_back({ cx - hw, cy - hh, cx + hw, cy + hh });
	}

	// A hollow square of four thin quads -- the radar frame.
	inline void push_frame(std::vector<px_quad> &out, float x, float y, float size, float thickness)
	{
		const float h = thickness * 0.5f;
		out.push_back({ x - h, y - h, x + size + h, y + h });
		out.push_back({ x - h, y + size - h, x + size + h, y + size + h });
		out.push_back({ x - h, y - h, x + h, y + size + h });
		out.push_back({ x + size - h, y - h, x + size + h, y + size + h });
	}

	// A ring of small quads: the "you are here" marker at the centre.
	inline void push_ring(std::vector<px_quad> &out, float cx, float cy, float radius, float thickness,
		int segments)
	{
		for (int i = 0; i < segments; ++i) {
			const float a = (static_cast<float>(i) / static_cast<float>(segments)) * 6.28318531f;
			push_quad(out, cx + std::cos(a) * radius, cy + std::sin(a) * radius, thickness, thickness);
		}
	}

	// Dots for every detected character, in radar pixels. Player centred.
	inline void build_marks(std::vector<px_quad> &out, const state_t &s, float view_h,
		bool through_colour_pass, bool track)
	{
		const float k = std::max(1.0f, view_h / 1080.0f);
		for (const mark &m : s.shown) {
			if (m.through_wall != through_colour_pass)
				continue;
			float p[2];
			if (!project(s, m.world, p))
				continue;
			const float fade = m.age == 0 ? 1.0f
				: std::max(0.0f, 1.0f - static_cast<float>(m.age) / 90.0f);
			if (fade <= 0.01f)
				continue;
			push_quad(out, p[0], p[1], 3.6f * k * fade, 3.6f * k * fade);

			const float yaw = track ? movement_yaw(m, s.yaw) : 0.0f;
			if (yaw != 0.0f) {
				const float len = 9.0f * k * fade;
				push_quad(out, p[0] + std::sin(yaw) * len, p[1] - std::cos(yaw) * len,
					1.6f * k, 1.6f * k);
			}
		}
	}

	// Our own state save/restore. The composite's overlay_state_guard also restores
	// what we change, but it is defined AFTER this file is included, and reusing it
	// would put the radar inside the mask pipeline's assumptions. This guard only
	// touches what the radar changes, and runs on every exit path.
	struct radar_state_guard
	{
		GLint program = 0, vao = 0, array_buffer = 0;
		GLint vp[4] = { 0, 0, 0, 0 };
		GLboolean blend = GL_FALSE, depth = GL_FALSE, scissor = GL_FALSE;
		GLint depth_func = GL_LESS;
		GLboolean depth_mask = GL_TRUE, color_mask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
		GLint blend_src = GL_ONE, blend_dst = GL_ZERO;
		GLint blend_eq = GL_FUNC_ADD;

		radar_state_guard()
		{
			if (gl.GetIntegerv == nullptr)
				return;
			gl.GetIntegerv(GL_CURRENT_PROGRAM, &program);
			gl.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
			gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
			gl.GetIntegerv(GL_VIEWPORT, vp);
			gl.GetIntegerv(GL_DEPTH_FUNC, &depth_func);
			gl.GetIntegerv(GL_BLEND_EQUATION, &blend_eq);
			gl.GetIntegerv(GL_BLEND_SRC_RGB, &blend_src);
			gl.GetIntegerv(GL_BLEND_DST_RGB, &blend_dst);
			if (gl.IsEnabled) {
				blend = gl.IsEnabled(GL_BLEND);
				depth = gl.IsEnabled(GL_DEPTH_TEST);
				scissor = gl.IsEnabled(GL_SCISSOR_TEST);
			}
			if (gl.GetBooleanv) {
				gl.GetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
				gl.GetBooleanv(GL_COLOR_WRITEMASK, color_mask);
			}
		}
		~radar_state_guard()
		{
			if (gl.GetIntegerv == nullptr || gl.UseProgram == nullptr)
				return;
			gl.UseProgram(static_cast<GLuint>(program));
			if (gl.BindVertexArray) gl.BindVertexArray(static_cast<GLuint>(vao));
			gl.BindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(array_buffer));
			gl.Viewport(vp[0], vp[1], vp[2], vp[3]);
			gl.DepthFunc(static_cast<GLenum>(depth_func));
			gl.DepthMask(depth_mask);
			gl.ColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
			gl.BlendEquation(static_cast<GLenum>(blend_eq));
			gl.BlendFunc(static_cast<GLenum>(blend_src), static_cast<GLenum>(blend_dst));
			if (depth == GL_TRUE) gl.Enable(GL_DEPTH_TEST); else gl.Disable(GL_DEPTH_TEST);
			if (blend == GL_TRUE) gl.Enable(GL_BLEND);      else gl.Disable(GL_BLEND);
			if (scissor == GL_TRUE) gl.Enable(GL_SCISSOR_TEST); else gl.Disable(GL_SCISSOR_TEST);
		}
	};

	inline void draw_px_quads(std::vector<px_quad> &quads, const float colour[4],
		float view_w, float view_h)
	{
		if (quads.empty() || !build() || gl.DrawArrays == nullptr)
			return;
		radar_gpu &g = gpu();

		radar_state_guard guard;
		GLint vp[4] = { 0, 0, 0, 0 };
		if (gl.GetIntegerv) gl.GetIntegerv(GL_VIEWPORT, vp);
		const float vx = static_cast<float>(vp[0]), vy = static_cast<float>(vp[1]);
		const float vw = vp[2] > 0 ? static_cast<float>(vp[2]) : view_w;
		const float vh = vp[3] > 0 ? static_cast<float>(vp[3]) : view_h;
		if (vw < 2.0f || vh < 2.0f)
			return;

		gl.UseProgram(g.program);
		gl.BindVertexArray(g.vao);
		gl.BindBuffer(GL_ARRAY_BUFFER, g.vbo);
		gl.Disable(GL_DEPTH_TEST);
		gl.Disable(GL_CULL_FACE);
		gl.Disable(GL_SCISSOR_TEST);
		gl.Enable(GL_BLEND);
		gl.BlendEquation(GL_FUNC_ADD);
		gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		gl.DepthMask(GL_FALSE);
		if (g.loc_colour >= 0)
			gl.Uniform4f(g.loc_colour, colour[0], colour[1], colour[2], colour[3]);

		for (const px_quad &q : quads) {
			if (g.loc_rect < 0)
				break;
			const float nx0 = (q.x0 - vx) / vw * 2.0f - 1.0f;
			const float nx1 = (q.x1 - vx) / vw * 2.0f - 1.0f;
			const float ny0 = 1.0f - (q.y1 - vy) / vh * 2.0f;
			const float ny1 = 1.0f - (q.y0 - vy) / vh * 2.0f;
			gl.Uniform4f(g.loc_rect, nx0, ny0, nx1 - nx0, ny1 - ny0);
			gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}

		// Drain our OWN GL errors. Leaving them in the queue made the composite's
		// "success = GetError() == GL_NO_ERROR" check fail on every frame after the
		// radar drew, which is the kind of cross-talk that ends in a black frame.
		if (gl.GetError)
			for (int i = 0; i < 8 && gl.GetError() != GL_NO_ERROR; ++i) {}
	}

	// Draw entry point, called from render_overlay() once per frame.
	//
	// SCISSOR (stage 112) -- everything the radar draws is clipped to the square's
	// own rectangle. A geometry or state mistake can then only ever affect the
	// radar square itself; it can never black out the frame. The scissor is set
	// against the real viewport and restored by the guard.
	inline void draw(bool marks, bool editor, int active_corner, bool track)
	{
		(void)editor;
		(void)active_corner;
		state_t &s = state();

		GLint vp[4] = { 0, 0, 0, 0 };
		if (gl.GetIntegerv == nullptr || gl.Scissor == nullptr)
			return;
		gl.GetIntegerv(GL_VIEWPORT, vp);
		const float vw = vp[2] > 0 ? static_cast<float>(vp[2]) : 1920.0f;
		const float vh = vp[3] > 0 ? static_cast<float>(vp[3]) : 1080.0f;
		if (vw < 2.0f || vh < 2.0f)
			return;

		// sane bounds: never trust the ini, a bad square must not become a screen
		const float size = std::min(std::max(1.0f, s.size), std::max(2.0f, vw));
		const float x = std::min(std::max(-size, s.x), vw);
		const float y = std::min(std::max(-size, s.y), vh);
		s.x = x;
		s.y = y;
		s.size = size;
		const float k = std::max(1.0f, vh / 1080.0f);

		// nothing to draw if the square is entirely off screen
		if (x + size <= 0.0f || y + size <= 0.0f || x >= vw || y >= vh)
			return;

		// clip to the square (plus the frame's half thickness), in GL's bottom-left
		// pixel space, intersected with the viewport
		const float pad = 4.0f * k;
		const float sx0 = std::max(0.0f, x - pad);
		const float sy0 = std::max(0.0f, y - pad);
		const float sx1 = std::min(vw, x + size + pad);
		const float sy1 = std::min(vh, y + size + pad);
		if (sx1 - sx0 < 1.0f || sy1 - sy0 < 1.0f)
			return;
		const GLint gl_y = static_cast<GLint>(vh - sy1);
		gl.Enable(GL_SCISSOR_TEST);
		gl.Scissor(static_cast<GLint>(sx0), gl_y,
			static_cast<GLsizei>(sx1 - sx0), static_cast<GLsizei>(sy1 - sy0));

		// background plate (alpha 0 by default -- opt in, so it can never darken
		// the corner of the game unless it was asked for)
		if (g_settings.radar_bg_colour[3] > 0.01f) {
			std::vector<px_quad> plate;
			plate.push_back({ x, y, x + size, y + size });
			draw_px_quads(plate, g_settings.radar_bg_colour, vw, vh);
		}

		// frame + centre ring
		{
			std::vector<px_quad> quads;
			push_frame(quads, x, y, size, std::max(1.0f, 1.4f * k));
			push_ring(quads, x + size * 0.5f, y + size * 0.5f, 5.0f * k, 1.5f * k, 16);
			draw_px_quads(quads, g_settings.radar_frame_colour, vw, vh);
		}

		if (marks) {
			s.x = x;
			s.y = y;
			s.size = size;
			for (int pass = 0; pass < 2; ++pass) {
				std::vector<px_quad> quads;
				build_marks(quads, s, vh, pass == 1, track);
				const float *col = pass == 1 ? g_settings.radar_wall_colour : g_settings.radar_dot_colour;
				draw_px_quads(quads, col, vw, vh);
			}
		}

		// facts, so a report can be diagnosed from the ReShade log instead of a guess
		static unsigned last_log = 0;
		const unsigned bodies = s.bodies.load(std::memory_order_relaxed);
		if (g_frame_id.load(std::memory_order_relaxed) - last_log > 600) {
			last_log = g_frame_id.load(std::memory_order_relaxed);
			radar_gpu &g = gpu();
			reshade::log::message(reshade::log::level::info,
				"[RADAR] on=1 program=%d ready=%d drawn=%u shown=%zu square=%.0f,%.0f size=%.0f viewport=%.0fx%.0f",
				static_cast<int>(g.program), g.ready ? 1 : 0, bodies, s.shown.size(),
				x, y, size, vw, vh);
		}
	}
}
