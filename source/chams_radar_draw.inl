/*
 * chams_radar_draw.inl -- draws the player-centred radar square.
 * created by hazu@alexainc
 *
 * Included from chams_gl.inl AFTER chams_radar_core.inl.
 *
 * WHY THIS DRAWS WITH glClear AND A SCISSOR (stage 113)
 * ----------------------------------------------------
 * Stage 111 gave the radar its own shader program, VAO, vertex buffer and vertex
 * attributes. That is the smallest amount of state that still touches the
 * pipeline the game is using, and it went wrong twice:
 *
 *   stage 111 -- program/VAO/blend state leaked into the game's own draws
 *   stage 112 -- the scissor was enabled by draw() before the state guard took
 *                its snapshot, so the guard "restored" our own scissor and the
 *                game stayed clipped to a 260 px square: the canvas outside the
 *                square was black while ReShade's ImGui still drew fine.
 *
 * So the radar now draws only with glScissor + glClear. No programs, no
 * shaders, no VAO, no vertex attributes, no blend equation, no uniforms. The
 * complete set of state involved is: the scissor box, the enable flag, and the
 * clear colour, and all three are captured before anything is touched and
 * restored afterwards. There is no code path left here that can corrupt a
 * matrix, an attribute binding or a texture unit.
 *
 * TRADE-OFF, stated plainly: glClear does not blend, so the radar is opaque
 * rather than alpha blended. An opaque radar that always draws beats a pretty
 * one that can black the game out.
 */
#pragma once

namespace chams_radar
{
	// Everything this file touches, captured before the first write.
	struct radar_state_guard
	{
		GLboolean scissor_enabled = GL_FALSE;
		GLint     scissor_box[4] = { 0, 0, 16384, 16384 };
		GLfloat   clear_colour[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		GLboolean colour_mask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
		GLint     viewport[4] = { 0, 0, 0, 0 };
		bool      captured = false;

		radar_state_guard()
		{
			if (gl.GetBooleanv == nullptr || gl.GetIntegerv == nullptr || gl.GetFloatv == nullptr)
				return;
			scissor_enabled = gl.IsEnabled ? gl.IsEnabled(GL_SCISSOR_TEST) : GL_FALSE;
			gl.GetIntegerv(GL_SCISSOR_BOX, scissor_box);
			gl.GetFloatv(GL_COLOR_CLEAR_VALUE, clear_colour);
			gl.GetBooleanv(GL_COLOR_WRITEMASK, colour_mask);
			gl.GetIntegerv(GL_VIEWPORT, viewport);
			captured = true;
		}
		~radar_state_guard()
		{
			if (!captured)
				return;
			// restore in the reverse order of the changes, and explicitly, so the
			// enable flag ends up exactly as the game left it
			gl.ClearColor(clear_colour[0], clear_colour[1], clear_colour[2], clear_colour[3]);
			gl.ColorMask(colour_mask[0], colour_mask[1], colour_mask[2], colour_mask[3]);
			gl.Scissor(scissor_box[0], scissor_box[1], scissor_box[2], scissor_box[3]);
			if (scissor_enabled == GL_TRUE)
				gl.Enable(GL_SCISSOR_TEST);
			else
				gl.Disable(GL_SCISSOR_TEST);
		}
	};

	inline bool can_draw()
	{
		return gl.Scissor != nullptr && gl.Clear != nullptr && gl.ClearColor != nullptr &&
			gl.Enable != nullptr && gl.Disable != nullptr && gl.GetIntegerv != nullptr;
	}

	// One opaque rectangle, in pixels from the top left of the radar's viewport.
	// Clipped to the radar square by the caller.
	inline void clear_rect(const GLint clip[4], float x0, float y0, float x1, float y1,
		const float colour[4], float vh)
	{
		// to GL's bottom-left space, and rounded outwards so a dot is never lost
		const float gx0 = std::floor(std::min(x0, x1));
		const float gx1 = std::ceil(std::max(x0, x1));
		const float gy0 = std::floor(std::min(y0, y1));
		const float gy1 = std::ceil(std::max(y0, y1));
		GLint rx = static_cast<GLint>(gx0);
		GLint ry = static_cast<GLint>(vh - gy1);
		GLsizei rw = static_cast<GLsizei>(gx1 - gx0);
		GLsizei rh = static_cast<GLsizei>(gy1 - gy0);
		if (rw <= 0 || rh <= 0)
			return;
		// intersect with the radar's clip rect; if the result is empty, skip
		const GLint cx0 = std::max(rx, clip[0]);
		const GLint cy0 = std::max(ry, clip[1]);
		const GLint cx1 = std::min(rx + rw, clip[0] + clip[2]);
		const GLint cy1 = std::min(ry + rh, clip[1] + clip[3]);
		if (cx1 <= cx0 || cy1 <= cy0)
			return;
		gl.Scissor(cx0, cy0, cx1 - cx0, cy1 - cy0);
		gl.ClearColor(colour[0], colour[1], colour[2], 1.0f);   // clear ignores blending
		gl.Clear(GL_COLOR_BUFFER_BIT);
	}

	inline void draw_rect(const GLint clip[4], float x0, float y0, float x1, float y1,
		const float colour[4], float vh)
	{
		if (!can_draw())
			return;
		clear_rect(clip, x0, y0, x1, y1, colour, vh);
	}

	// A hollow frame of four thin edges.
	inline void draw_frame(const GLint clip[4], float x, float y, float size, float thickness,
		const float colour[4], float vh)
	{
		const float h = std::max(1.0f, thickness);
		draw_rect(clip, x, y, x + size, y + h, colour, vh);              // top
		draw_rect(clip, x, y + size - h, x + size, y + size, colour, vh); // bottom
		draw_rect(clip, x, y, x + h, y + size, colour, vh);              // left
		draw_rect(clip, x + size - h, y, x + size, y + size, colour, vh); // right
	}

	// The "you are here" marker: a small square ring at the centre.
	inline void draw_centre(const GLint clip[4], float cx, float cy, float radius, float thickness,
		const float colour[4], float vh)
	{
		const float t = std::max(1.0f, thickness);
		draw_rect(clip, cx - radius, cy - radius, cx + radius, cy - radius + t, colour, vh);
		draw_rect(clip, cx - radius, cy + radius - t, cx + radius, cy + radius, colour, vh);
		draw_rect(clip, cx - radius, cy - radius, cx - radius + t, cy + radius, colour, vh);
		draw_rect(clip, cx + radius - t, cy - radius, cx + radius, cy + radius, colour, vh);
	}

	// Every detected character, in radar pixels. The player is the centre.
	//
	// A mark the game is currently drawing (age 0) is a filled square in the dot
	// colour. A mark that has stopped being drawn -- behind or beside the camera,
	// culled by the game -- is a hollow square in the wall colour. Both are on the
	// radar; the difference is stated on screen rather than hidden.
	inline void draw_marks(const GLint clip[4], const state_t &s, float vh, bool track)
	{
		const float k = std::max(1.0f, vh / 1080.0f);
		for (const mark &m : s.shown) {
			float p[2];
			if (!project(s, m.world, p))
				continue;
			const float half = 2.5f * k;
			const bool invisible = (m.age > 0) || m.through_wall;
			const float *col = invisible ? g_settings.radar_wall_colour
			                             : g_settings.radar_dot_colour;
			if (!invisible) {
				draw_rect(clip, p[0] - half, p[1] - half, p[0] + half, p[1] + half, col, vh);
			} else {
				// hollow: four 1 px edges, so "not currently drawn" is obvious
				const float t = std::max(1.0f, k);
				draw_rect(clip, p[0] - half, p[1] - half, p[0] + half, p[1] - half + t, col, vh);
				draw_rect(clip, p[0] - half, p[1] + half - t, p[0] + half, p[1] + half, col, vh);
				draw_rect(clip, p[0] - half, p[1] - half, p[0] - half + t, p[1] + half, col, vh);
				draw_rect(clip, p[0] + half - t, p[1] - half, p[0] + half, p[1] + half, col, vh);
			}

			if (!track)
				continue;
			const float yaw = movement_yaw(m, s.yaw);
			if (yaw == 0.0f)
				continue;
			const float len = 8.0f * k;
			const float tx = p[0] + std::sin(yaw) * len;
			const float ty = p[1] - std::cos(yaw) * len;
			draw_rect(clip, tx - 1.5f * k, ty - 1.5f * k, tx + 1.5f * k, ty + 1.5f * k, col, vh);
		}
	}

	// A short tick at the top edge marking where the camera is looking, so the
	// square reads as "me at the centre, up is where I face".
	inline void draw_heading(const GLint clip[4], float x, float y, float size, float thickness,
		const float colour[4], float vh)
	{
		const float t = std::max(1.0f, thickness);
		const float cx = x + size * 0.5f;
		const float span = size * 0.10f;
		draw_rect(clip, cx - t, y, cx + t, y + span, colour, vh);
	}

	// Draw entry point, called from render_overlay() once per frame.
	inline void draw(bool marks, bool editor, int active_corner, bool track)
	{
		(void)editor;
		(void)active_corner;
		if (!can_draw() || gl.GetBooleanv == nullptr || gl.GetFloatv == nullptr)
			return;

		state_t &s = state();
		radar_state_guard guard;
		if (!guard.captured)
			return;

		const float vw = guard.viewport[2] > 0 ? static_cast<float>(guard.viewport[2]) : 1920.0f;
		const float vh = guard.viewport[3] > 0 ? static_cast<float>(guard.viewport[3]) : 1080.0f;
		if (vw < 2.0f || vh < 2.0f)
			return;

		// never trust the ini: clamp the square to something drawable
		const float size = std::min(std::max(40.0f, s.size), std::min(vw, vh));
		const float x = std::min(std::max(0.0f, s.x), std::max(0.0f, vw - size));
		const float y = std::min(std::max(0.0f, s.y), std::max(0.0f, vh - size));
		s.x = x;
		s.y = y;
		s.size = size;
		const float k = std::max(1.0f, vh / 1080.0f);

		// the clip rect: the square itself, in GL's bottom-left pixel space
		const GLint clip[4] = { static_cast<GLint>(x), static_cast<GLint>(vh - (y + size)),
			static_cast<GLsizei>(size), static_cast<GLsizei>(size) };
		gl.Enable(GL_SCISSOR_TEST);
		gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		gl.Scissor(clip[0], clip[1], clip[2], clip[3]);
		gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		gl.Clear(GL_COLOR_BUFFER_BIT);   // the plate, now that we are clipped to it

		draw_frame(clip, x, y, size, 1.5f * k, g_settings.radar_frame_colour, vh);
		draw_heading(clip, x, y, size, 1.5f * k, g_settings.radar_frame_colour, vh);
		draw_centre(clip, x + size * 0.5f, y + size * 0.5f, 5.0f * k, 1.5f * k,
			g_settings.radar_frame_colour, vh);
		if (marks)
			draw_marks(clip, s, vh, track);

		// facts, so a report can be diagnosed from the log instead of a guess
		static unsigned last_log = 0;
		const unsigned now_frame = g_frame_id.load(std::memory_order_relaxed);
		if (now_frame - last_log > 600) {
			last_log = now_frame;
			reshade::log::message(reshade::log::level::info,
				"[RADAR] drawn=%u shown=%zu square=%.0f,%.0f size=%.0f viewport=%.0fx%.0f scissor_was=%d "
				"rotate=%s cam_yaw=%.2f cam_yaw_valid=%d px_per_m=%.2f",
				s.bodies.load(std::memory_order_relaxed), s.shown.size(), x, y, size, vw, vh,
				guard.scissor_enabled == GL_TRUE ? 1 : 0,
				g_settings.radar_rotate_with_camera ? "camera" : "manual",
				s.cam_yaw, s.cam_yaw_valid ? 1 : 0, px_per_m(s));
		}
		// guard destructor restores scissor enable + box + clear colour + colour mask
	}
}
