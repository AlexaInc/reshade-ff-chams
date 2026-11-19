/*
 * chams_radar.inl -- minimap marks for detected characters.
 * created by hazu@alexainc
 *
 * WHAT THIS IS
 * ------------
 * The chams already classify every character draw (program_is_character) and
 * already read unity_ObjectToWorld for the distance heuristic, so the world
 * position of every detected body is available for free. This file collects one
 * world position per detected body per frame, projects it into screen space with
 * the same camera matrices, and then draws a dot inside the minimap rectangle
 * the user marked with the pentool calibration.
 *
 * WHY A CALIBRATION AND NOT A SCALE CONSTANT
 * -----------------------------------------
 * The minimap is drawn by the game's own UI, not by us: we never see its
 * transform, so there is no scale to read at runtime and none to extract from
 * the game package (the map scale is a per-match HUD value). The user marks the
 * four corners of the minimap once; that quad IS the mapping, so rotation,
 * scale, aspect and any skew are all handled by construction, and re-marking
 * after a resolution change takes seconds.
 *
 * WORLD -> MAP SPACE
 * ------------------
 *   p      = (x, z) of the world position, relative to the camera (map is top
 *            down, so world Y is height and never enters the mapping)
 *   local  = rotate(-yaw) so the map's "up" is the direction the user marked
 *   u,v    = local / half_extent  -> 0..1 inside the marked quad
 *   pixel  = bilinear interpolation of the four marked corners by (u, v)
 *
 * A bilinear quad (not a homography) on purpose: the four corners of a minimap
 * are always a parallelogram on screen, and bilinear on a parallelogram is exact.
 */
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <vector>

namespace chams_radar
{
	struct mark
	{
		float world[2] = { 0.0f, 0.0f }; // x, z in world units
		float screen[2] = { 0.0f, 0.0f };
		float prev_world[2] = { 0.0f, 0.0f };
		float prev_screen[2] = { 0.0f, 0.0f };
		float yaw = 0.0f;                // movement direction, radians, map space
		bool  through_wall = false;      // drawn behind something (colour split)
		float last_seen = 0.0f;          // seconds, for the fade
		unsigned age = 0;                // frames since last seen
	};

	// One quad in screen pixels, marked with the pentool.
	struct quad
	{
		float x[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // TL, TR, BR, BL
		float y[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		bool  marked = false;

		bool valid() const
		{
			if (!marked) return false;
			// reject a degenerate quad (all four corners on top of each other)
			float minx = x[0], maxx = x[0], miny = y[0], maxy = y[0];
			for (int i = 1; i < 4; ++i) {
				minx = std::min(minx, x[i]); maxx = std::max(maxx, x[i]);
				miny = std::min(miny, y[i]); maxy = std::max(maxy, y[i]);
			}
			return (maxx - minx) > 16.0f && (maxy - miny) > 16.0f;
		}

		// bilinear: u, v in 0..1, 0,0 = TL corner
		void at(float u, float v, float out[2]) const
		{
			const float top_x = x[0] + (x[1] - x[0]) * u;
			const float top_y = y[0] + (y[1] - y[0]) * u;
			const float bot_x = x[3] + (x[2] - x[3]) * u;
			const float bot_y = y[3] + (y[2] - y[3]) * u;
			out[0] = top_x + (bot_x - top_x) * v;
			out[1] = top_y + (bot_y - top_y) * v;
		}
	};

	// Per-frame collection. Draws run on the render thread, the overlay composite
	// runs on the swap thread, so the list is handed over with a short lock and
	// the dots are drawn from the snapshot.
	struct state_t
	{
		std::mutex           lock;
		std::vector<mark>    live;      // collected this frame, being built
		std::vector<mark>    shown;     // snapshot the composite draws from
		quad                 map;       // the marked minimap
		float                radius = 60.0f;   // marked map half extent, metres
		float                yaw = 0.0f;       // map orientation, radians (user set)
		float                camera[3] = { 0.0f, 0.0f, 0.0f };
		float                cursor[2] = { 0.0f, 0.0f };   // published by the menu
		bool                 have_cursor = false;
		bool                 tracker = false;  // tracker had a body last frame
		std::atomic<unsigned> bodies{ 0 };
	};
	inline state_t &state()
	{
		static state_t s;
		return s;
	}

	inline double now_seconds()
	{
		using clock = std::chrono::steady_clock;
		static const clock::time_point start = clock::now();
		return std::chrono::duration<double>(clock::now() - start).count();
	}

	// ------------------------------------------------------------- collection
	// Called from draw_chams() for every draw that already passed character
	// detection and was NOT excluded as the user's own character. Twenty-odd
	// draws belong to one body, so positions are clustered: a new position within
	// kCluster metres of a known one updates it instead of adding a dot.
	inline constexpr float kCluster = 1.2f;

	inline void note_character(const float world_pos[3], bool through_wall)
	{
		state_t &s = state();
		const float wx = world_pos[0], wz = world_pos[2];
		if (!std::isfinite(wx) || !std::isfinite(wz) || (wx == 0.0f && wz == 0.0f))
			return;
		std::lock_guard<std::mutex> lock(s.lock);
		for (mark &m : s.live) {
			const float dx = m.world[0] - wx, dz = m.world[1] - wz;
			if (dx * dx + dz * dz <= kCluster * kCluster) {
				m.world[0] = wx;
				m.world[1] = wz;
				m.through_wall = m.through_wall && through_wall;
				return;
			}
		}
		mark m;
		m.world[0] = wx;
		m.world[1] = wz;
		m.through_wall = through_wall;
		m.last_seen = static_cast<float>(now_seconds());
		s.live.push_back(m);
	}

	// Camera position, taken from the same matrices the distance heuristic uses.
	inline void note_camera(const float cam[3])
	{
		if (!std::isfinite(cam[0]) || !std::isfinite(cam[2])) return;
		state_t &s = state();
		std::lock_guard<std::mutex> lock(s.lock);
		s.camera[0] = cam[0];
		s.camera[1] = cam[1];
		s.camera[2] = cam[2];
	}

	// Called once per frame from begin_frame(): the collected positions become the
	// drawn snapshot, and the previous snapshot keeps the dots alive for a moment
	// so a body that skipped a frame does not blink.
	inline void frame_commit(float radius_m, float map_yaw)
	{
		state_t &s = state();
		std::lock_guard<std::mutex> lock(s.lock);
		for (const mark &fresh : s.live) {
			bool matched = false;
			for (mark &old : s.shown) {
				const float dx = old.world[0] - fresh.world[0], dz = old.world[1] - fresh.world[1];
				if (dx * dx + dz * dz <= kCluster * kCluster) {
					old.prev_world[0] = old.world[0];
					old.prev_world[1] = old.world[1];
					old.world[0] = fresh.world[0];
					old.world[1] = fresh.world[1];
					old.through_wall = fresh.through_wall;
					old.last_seen = fresh.last_seen;
					old.age = 0;
					matched = true;
					break;
				}
			}
			if (!matched) {
				mark m = fresh;
				m.prev_world[0] = fresh.world[0];
				m.prev_world[1] = fresh.world[1];
				s.shown.push_back(m);
			}
		}
		for (mark &old : s.shown) {
			bool seen = false;
			for (const mark &fresh : s.live) {
				const float dx = old.world[0] - fresh.world[0], dz = old.world[1] - fresh.world[1];
				if (dx * dx + dz * dz <= kCluster * kCluster) { seen = true; break; }
			}
			if (!seen) ++old.age;
		}
		// a body that has not been seen for ~1.5 s is gone
		s.shown.erase(std::remove_if(s.shown.begin(), s.shown.end(),
			[](const mark &m) { return m.age > 90; }), s.shown.end());
		s.live.clear();
		s.bodies.store(static_cast<unsigned>(s.shown.size()));

		s.yaw = map_yaw;
		s.radius = std::max(1.0f, radius_m);
	}

	// ------------------------------------------------------------- projection
	inline bool project(const quad &q, const float cam[3], float yaw, float radius,
		const float world[2], float out[2])
	{
		if (!q.valid() || !(radius > 0.1f))
			return false;
		const float dx = world[0] - cam[0];
		const float dz = world[1] - cam[2];
		// camera yaw: forward is -Z at yaw 0 in Unity's left handed system
		const float c = std::cos(-yaw), s = std::sin(-yaw);
		const float rx = dx * c - dz * s;
		const float rz = dx * s + dz * c;
		const float u = 0.5f + rx / (2.0f * radius);
		const float v = 0.5f - rz / (2.0f * radius);
		if (u < -0.15f || u > 1.15f || v < -0.15f || v > 1.15f)
			return false;                        // outside the marked map
		q.at(std::clamp(u, 0.0f, 1.0f), std::clamp(v, 0.0f, 1.0f), out);
		return true;
	}

	// Facing: the last two positions give the movement direction in map space.
	inline float movement_yaw(const mark &m, float cam_yaw)
	{
		const float dx = m.world[0] - m.prev_world[0];
		const float dz = m.world[1] - m.prev_world[1];
		if (dx * dx + dz * dz < 1e-6f)
			return 0.0f;
		return std::atan2(dx, -dz) - cam_yaw;
	}
}


// ---------------------------------------------------------------------------
// Settings bridge + per-frame helpers. Kept here so chams_overlay.inl makes a
// single call and the menu edits the settings struct directly.
// ---------------------------------------------------------------------------
namespace chams_radar
{
	inline void sync_from_settings()
	{
		state_t &s = state();
		s.map.marked = g_settings.radar_map_marked;
		for (int i = 0; i < 4; ++i) {
			s.map.x[i] = g_settings.radar_map_x[i];
			s.map.y[i] = g_settings.radar_map_y[i];
		}
	}

	// Corner placement, driven by the menu: that unit owns the cursor (it is the
	// only one with ImGui), so it hands over the position and the two button edges.
	inline void editor_input(bool editor, float mx, float my, bool left_click, bool right_click,
		int &active_corner, bool &marked)
	{
		if (!editor)
			return;
		state_t &s = state();
		if (left_click && active_corner >= 0 && active_corner < 4) {
			g_settings.radar_map_x[active_corner] = mx;
			g_settings.radar_map_y[active_corner] = my;
			s.map.x[active_corner] = mx;
			s.map.y[active_corner] = my;
			active_corner = (active_corner + 1) % 4;
			g_settings.radar_corner = active_corner;
			if (active_corner == 0)
				marked = true;              // all four corners placed
		}
		if (right_click) {
			active_corner = (active_corner + 1) % 4;
			g_settings.radar_corner = active_corner;
		}
	}
}
