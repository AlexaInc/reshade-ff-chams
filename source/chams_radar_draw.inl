namespace chams_radar
{
	// defined in chams.cpp, written by the menu
	extern std::atomic<float> g_radar_cursor_x;
	extern std::atomic<float> g_radar_cursor_y;
	extern std::atomic<bool>  g_radar_cursor_valid;

	// One screen-space quad, in pixels, from the GL viewport's top left corner.
	struct px_quad { float x0, y0, x1, y1; };

	inline void push_px_quad(std::vector<px_quad> &out, float cx, float cy, float hw, float hh)
	{
		out.push_back({ cx - hw, cy - hh, cx + hw, cy + hh });
	}

	// A dot with a direction tick: a round-ish body quad plus a thinner quad
	// pointing along `yaw`, so movement is readable without text.
	inline void build_marks(std::vector<px_quad> &out, const state_t &s, const float cam[3],
		float view_w, float view_h, bool through_colour_pass, bool track)
	{
		for (const mark &m : s.shown) {
			float p[2];
			if (!project(s.map, cam, s.yaw, s.radius, m.world, p))
				continue;
			if (m.through_wall != through_colour_pass)
				continue;                       // two colours: one pass each
			const float k = std::max(1.0f, view_h / 1080.0f);
			const float r = 3.5f * k;
			// fade out over the last ~1 s so a body that was lost does not pop
			const float fade = m.age == 0 ? 1.0f : std::max(0.0f, 1.0f - static_cast<float>(m.age) / 90.0f);
			if (fade <= 0.01f)
				continue;
			push_px_quad(out, p[0], p[1], r * fade, r * fade);

			const float yaw = track ? movement_yaw(m, s.yaw) : 0.0f;
			if (yaw != 0.0f) {
				const float len = 9.0f * k * fade;
				const float tip_x = p[0] + std::sin(yaw) * len;
				const float tip_y = p[1] - std::cos(yaw) * len;
				push_px_quad(out, tip_x, tip_y, 1.6f * k, 1.6f * k);
			}
		}
	}

	// The pentool editor: four anchors, four edges, and a line to the mouse.
	inline void build_editor(std::vector<px_quad> &out, const quad &q, const float mouse[2],
		bool have_mouse, int active_corner, float view_h)
	{
		const float k = std::max(1.0f, view_h / 1080.0f);
		const float anchor = 5.0f * k;
		for (int i = 0; i < 4; ++i) {
			push_px_quad(out, q.x[i], q.y[i], anchor, anchor);
			// active corner: bigger, so the eye finds it
			if (i == active_corner)
				push_px_quad(out, q.x[i], q.y[i], anchor * 2.0f, anchor * 2.0f);
		}
		// edges: short segments along each side (a quad per step, cheap and crude)
		const float step = 3.0f * k;
		for (int i = 0; i < 4; ++i) {
			const int j = (i + 1) % 4;
			const float dx = q.x[j] - q.x[i], dy = q.y[j] - q.y[i];
			const float len = std::sqrt(dx * dx + dy * dy);
			const int n = static_cast<int>(len / (step * 2.0f));
			for (int t = 1; t < n; ++t) {
				const float f = static_cast<float>(t) / static_cast<float>(n);
				push_px_quad(out, q.x[i] + dx * f, q.y[i] + dy * f, step * 0.5f, step * 0.5f);
			}
		}
		if (have_mouse && active_corner >= 0 && active_corner < 4) {
			const float dx = mouse[0] - q.x[active_corner], dy = mouse[1] - q.y[active_corner];
			const float len = std::sqrt(dx * dx + dy * dy);
			const int n = static_cast<int>(len / (step * 2.0f));
			for (int t = 0; t < n; ++t) {
				const float f = static_cast<float>(t) / static_cast<float>(n);
				push_px_quad(out, q.x[active_corner] + dx * f, q.y[active_corner] + dy * f,
					step * 0.35f, step * 0.35f);
			}
		}
	}

	// Draw a list of pixel quads with the overlay program. u_mode: 0 = solid
	// colour (dots), 1 = outline only (editor edges), so the same shader serves
	// both and the game image stays visible under the editor.
	inline void draw_px_quads(std::vector<px_quad> &quads, const float colour[4],
		float view_w, float view_h, bool additive)
	{
		if (quads.empty() || gl.UseProgram == nullptr)
			return;
		overlay_gpu &g = ov();
		if (!g.ready || g.program == 0)
			return;

		overlay_state_guard guard;
		GLint vp[4] = { 0, 0, 0, 0 };
		gl.GetIntegerv(GL_VIEWPORT, vp);
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
		if (additive) {
			gl.BlendEquation(GL_FUNC_ADD);
			gl.BlendFunc(GL_ONE, GL_ONE);
		} else {
			gl.BlendEquation(GL_FUNC_ADD);
			gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
		gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		gl.DepthMask(GL_FALSE);

		// The quad shader draws a fullscreen quad from 0..1 corners; shifting and
		// scaling it in NDC places it anywhere on screen. u_mask is not sampled
		// for this pass, so bind the mask texture only to keep the sampler valid.
		if (g.loc_colour >= 0)
			gl.Uniform4f(g.loc_colour, colour[0], colour[1], colour[2], colour[3]);
		for (const px_quad &q : quads) {
			// pixel -> NDC (y flipped: GL origin is bottom left)
			const float nx0 = (q.x0 - vx) / vw * 2.0f - 1.0f;
			const float nx1 = (q.x1 - vx) / vw * 2.0f - 1.0f;
			const float ny0 = 1.0f - (q.y1 - vy) / vh * 2.0f;
			const float ny1 = 1.0f - (q.y0 - vy) / vh * 2.0f;
			if (g.loc_offset >= 0) {
				// scale = quad size in NDC, offset = quad centre in NDC
				gl.Uniform4f(g.loc_offset, (nx1 - nx0) * 0.5f, (ny1 - ny0) * 0.5f,
					(nx0 + nx1) * 0.5f, (ny0 + ny1) * 0.5f);
			}
			gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}
	}

	// Draw entry point: two colour passes for the marks, then the pentool editor.
	inline void draw(bool marks, bool editor, int active_corner, bool track)
	{
		GLint vp[4] = { 0, 0, 0, 0 };
		gl.GetIntegerv(GL_VIEWPORT, vp);
		const float vw = vp[2] > 0 ? static_cast<float>(vp[2]) : 1920.0f;
		const float vh = vp[3] > 0 ? static_cast<float>(vp[3]) : 1080.0f;
		state_t &s = state();

		std::vector<px_quad> quads;
		if (marks) {
			for (int pass = 0; pass < 2; ++pass) {
				quads.clear();
				build_marks(quads, s, s.camera, vw, vh, pass == 1, track);
				const float *col = pass == 1 ? g_settings.radar_wall_colour : g_settings.radar_dot_colour;
				draw_px_quads(quads, col, vw, vh, false);
			}
		}
		if (editor) {
			quads.clear();
			const float mouse[2] = { g_radar_cursor_x.load(std::memory_order_relaxed),
				g_radar_cursor_y.load(std::memory_order_relaxed) };
			build_editor(quads, s.map, mouse, g_radar_cursor_valid.load(std::memory_order_relaxed),
				active_corner, vh);
			const float col[4] = { 0.58f, 0.91f, 0.76f, 0.85f };
			draw_px_quads(quads, col, vw, vh, true);
		}
	}
}
