/*
 * chams_radar_draw.inl -- draws the player-centred radar square.
 * created by hazu@alexainc
 *
 * Included from chams_gl.inl AFTER chams_overlay.inl, because it needs
 * overlay_gpu / ov() / overlay_state_guard. Runs in the same wglSwapBuffers
 * window as the mask composite, on the game's final image.
 *
 * Everything is drawn with the overlay program's fullscreen quad: the square
 * frame, the centre ring (the player), one dot per detected character and a
 * direction tick. No shader of its own, no text, no buffers.
 */
#pragma once

namespace chams_radar
{
	// One screen-space quad, in pixels, from the GL viewport's top left corner.
	struct px_quad { float x0, y0, x1, y1; };

	inline void push_quad(std::vector<px_quad> &out, float cx, float cy, float hw, float hh)
	{
		out.push_back({ cx - hw, cy - hh, cx + hw, cy + hh });
	}

	// A hollow square of four thin quads -- the radar frame.
	inline void push_frame(std::vector<px_quad> &out, float x, float y, float size, float thickness)
	{
		const float h = thickness * 0.5f;
		out.push_back({ x - h, y - h, x + size + h, y + h });                 // top
		out.push_back({ x - h, y + size - h, x + size + h, y + size + h });   // bottom
		out.push_back({ x - h, y - h, x + h, y + size + h });                 // left
		out.push_back({ x + size - h, y - h, x + size + h, y + size + h });   // right
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

	// Dots for every detected character, in radar pixels. Player centred: the
	// centre of the square is the player, everything else is relative.
	inline void build_marks(std::vector<px_quad> &out, const state_t &s, float view_h,
		bool through_colour_pass, bool track)
	{
		const float k = std::max(1.0f, view_h / 1080.0f);
		for (const mark &m : s.shown) {
			if (m.through_wall != through_colour_pass)
				continue;
			float p[2];
			if (!project(s, m.world, p))
				continue;                        // outside the square
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

	inline void draw_px_quads(std::vector<px_quad> &quads, const float colour[4],
		float view_w, float view_h, bool additive)
	{
		if (quads.empty() || gl.UseProgram == nullptr || gl.DrawArrays == nullptr)
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
		gl.BlendEquation(GL_FUNC_ADD);
		if (additive)
			gl.BlendFunc(GL_ONE, GL_ONE);
		else
			gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		gl.DepthMask(GL_FALSE);

		if (g.loc_colour >= 0)
			gl.Uniform4f(g.loc_colour, colour[0], colour[1], colour[2], colour[3]);
		for (const px_quad &q : quads) {
			if (g.loc_offset < 0)
				break;
			const float nx0 = (q.x0 - vx) / vw * 2.0f - 1.0f;
			const float nx1 = (q.x1 - vx) / vw * 2.0f - 1.0f;
			const float ny0 = 1.0f - (q.y1 - vy) / vh * 2.0f;
			const float ny1 = 1.0f - (q.y0 - vy) / vh * 2.0f;
			gl.Uniform4f(g.loc_offset, (nx1 - nx0) * 0.5f, (ny1 - ny0) * 0.5f,
				(nx0 + nx1) * 0.5f, (ny0 + ny1) * 0.5f);
			gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}
	}

	// Draw entry point, called from render_overlay() once per frame.
	inline void draw(bool marks, bool editor, int active_corner, bool track)
	{
		(void)editor;
		(void)active_corner;
		GLint vp[4] = { 0, 0, 0, 0 };
		gl.GetIntegerv(GL_VIEWPORT, vp);
		const float vw = vp[2] > 0 ? static_cast<float>(vp[2]) : 1920.0f;
		const float vh = vp[3] > 0 ? static_cast<float>(vp[3]) : 1080.0f;
		state_t &s = state();
		const float k = std::max(1.0f, vh / 1080.0f);

		std::vector<px_quad> quads;

		// frame + centre ring, in the "plate" colour
		quads.clear();
		push_frame(quads, s.x, s.y, s.size, std::max(1.0f, 1.4f * k));
		push_ring(quads, s.x + s.size * 0.5f, s.y + s.size * 0.5f, 5.0f * k, 1.5f * k, 16);
		draw_px_quads(quads, g_settings.radar_frame_colour, vw, vh, false);

		if (!marks)
			return;
		for (int pass = 0; pass < 2; ++pass) {
			quads.clear();
			build_marks(quads, s, vh, pass == 1, track);
			const float *col = pass == 1 ? g_settings.radar_wall_colour : g_settings.radar_dot_colour;
			draw_px_quads(quads, col, vw, vh, false);
		}
	}
}
