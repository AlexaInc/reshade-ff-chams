/*
 * chams_radar_core.inl -- player-centred radar built from captured matrices.
 * created by hazu@alexainc
 *
 * WHAT THIS IS
 * ------------
 * A radar square of our own, drawn outside the game's own map: no game minimap
 * texture, no pentool quad. The player is pinned to the centre of the square and
 * every detected character is placed around it from its world position, which the
 * chams already read (unity_ObjectToWorld) while classifying draws.
 *
 * SCALE
 * -----
 * 1:1 and uniform: one pixel is the same number of metres in x and z, so a world
 * square stays a square and distances are readable. Two knobs:
 *   size  pixels -- how big the square is (changed by dragging its corner)
 *   area  metres -- how much world fits across it (the map area)
 * so px_per_m = size / area.
 *
 * POSITION
 * --------
 * The square can be dragged while the ReShade menu is open. The menu is the only
 * unit with ImGui, so dragging happens there and the result lands in the
 * settings; with the menu closed no code path can move it, so the position is
 * locked by construction.
 */
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

namespace chams_radar
{
	struct mark
	{
		float world[2] = { 0.0f, 0.0f };      // x, z in world metres
		float prev_world[2] = { 0.0f, 0.0f };
		float yaw = 0.0f;                     // movement direction, radar space
		bool  through_wall = false;
		float last_seen = 0.0f;
		unsigned age = 0;                     // frames since last actually drawn
		float cached_cam[3] = { 0.0f, 0.0f, 0.0f };  // camera position at last sighting
	};

	struct state_t
	{
		std::mutex        lock;
		std::vector<mark> live;      // collected this frame
		std::vector<mark> shown;     // snapshot the draw uses

		float camera[3] = { 0.0f, 0.0f, 0.0f };
		float yaw = 0.0f;            // radar orientation, radians (user set)
		float cam_yaw = 0.0f;        // where the camera looks, radians (from MATRIX_V)
		bool  cam_yaw_valid = false; // false until a view matrix has been decoded

		float x = 40.0f, y = 40.0f;  // top left corner, pixels
		float size = 260.0f;         // square side, pixels
		float area_m = 120.0f;       // metres across the square

		std::atomic<unsigned> bodies{ 0 };
	};
	inline state_t &state()
	{
		static state_t s;
		return s;
	}

	inline void draw(bool marks, bool editor, int active_corner, bool track);
	inline void sync_from_settings();

	inline double now_seconds()
	{
		using clock = std::chrono::steady_clock;
		static const clock::time_point start = clock::now();
		return std::chrono::duration<double>(clock::now() - start).count();
	}

	inline constexpr float kCluster = 1.2f;   // metres: one body, one mark
	// A mark that stops being drawn is held while it stays near the camera
	// position it was last seen from (it is behind / beside the view, culled by
	// the game and therefore invisible to the classifier). A mark that moved
	// beyond kBodiesCulledRadius, or that was drawn behind a wall, is dropped
	// quickly so stale dots do not linger.
	inline constexpr unsigned kHoldFrames = 240;   // ~4 s at 60 fps
	inline constexpr unsigned kDropFrames = 90;
	inline constexpr float    kBodiesCulledRadius = 8.0f;  // metres from last-seen camera

	// ------------------------------------------------------------- collection
	// Camera facing from the view matrix. A Unity view matrix is the inverse of
	// the camera's world transform, so its rows (view_m[0..2], [4..6], [8..10])
	// are the camera's right/up/forward axes expressed in world space. No
	// Euler-convention guessing needed.
	//   forward world xz = (-m[2], -m[8])   (matrix is column major)
	//   atan2(x, z) gives the same angle convention as project(), so a mark
	//   straight ahead lands directly above the centre of the square.
	inline bool heading_from_view(const float view_m[16], float *out_yaw)
	{
		const float fx = -view_m[2], fz = -view_m[8];
		if (!std::isfinite(fx) || !std::isfinite(fz))
			return false;
		if (fx * fx + fz * fz < 1e-6f)      // camera looking straight down: no usable heading
			return false;
		if (out_yaw != nullptr)
			*out_yaw = std::atan2(fx, fz);
		return true;
	}

	inline void note_camera(const float cam[3], const float view_m[16] = nullptr)
	{
		if (!std::isfinite(cam[0]) || !std::isfinite(cam[2]))
			return;
		state_t &s = state();
		std::lock_guard<std::mutex> lock(s.lock);
		s.camera[0] = cam[0];
		s.camera[1] = cam[1];
		s.camera[2] = cam[2];
		float yaw = 0.0f;
		if (view_m != nullptr && heading_from_view(view_m, &yaw)) {
			s.cam_yaw = yaw;
			s.cam_yaw_valid = true;
		}
	}

	inline void note_character(const float world_pos[2], bool through_wall)
	{
		if (!std::isfinite(world_pos[0]) || !std::isfinite(world_pos[1]))
			return;
		if (world_pos[0] == 0.0f && world_pos[1] == 0.0f)
			return;
		state_t &s = state();
		std::lock_guard<std::mutex> lock(s.lock);
		for (mark &m : s.live) {
			const float dx = m.world[0] - world_pos[0], dz = m.world[1] - world_pos[1];
			if (dx * dx + dz * dz <= kCluster * kCluster) {
				m.world[0] = world_pos[0];
				m.world[1] = world_pos[1];
				m.through_wall = m.through_wall && through_wall;
				return;
			}
		}
		mark m;
		m.world[0] = world_pos[0];
		m.world[1] = world_pos[1];
		m.through_wall = through_wall;
		m.last_seen = static_cast<float>(now_seconds());
		std::memcpy(m.cached_cam, s.camera, sizeof(m.cached_cam));
		s.live.push_back(m);
	}

	// --------------------------------------------------------------- per frame
	inline void frame_commit(float area_m, float radar_yaw)
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
			if (seen) continue;

			// STAGE 114 -- body memory.
			// The game culls anything the camera cannot see, and a culled body is
			// never drawn, so it can never be classified. A mark that is close to
			// where it was last drawn is therefore almost certainly behind or
			// beside the camera rather than gone: hold it, frozen, for kHoldFrames.
			// A body that genuinely walked away stops matching and ages out fast.
			++old.age;
			const float moved = std::sqrt(
				(old.world[0] - old.cached_cam[0]) * (old.world[0] - old.cached_cam[0]) +
				(old.world[1] - old.cached_cam[2]) * (old.world[1] - old.cached_cam[2]));
			old.through_wall = moved > kBodiesCulledRadius;   // out of culling range: not visible
		}
		s.shown.erase(std::remove_if(s.shown.begin(), s.shown.end(),
			[](const mark &m) { return m.age > (m.through_wall ? kDropFrames : kHoldFrames); }),
			s.shown.end());
		s.live.clear();
		s.bodies.store(static_cast<unsigned>(s.shown.size()));
		s.yaw = radar_yaw;
		s.area_m = std::max(5.0f, area_m);
	}

	// ------------------------------------------------------------- projection
	inline float px_per_m(const state_t &s)
	{
		return (s.area_m > 0.1f) ? (s.size / s.area_m) : 1.0f;
	}

	// World metres -> radar pixels, player centred, 1:1 (same scale on x and z).
	// The rotation used on screen: the camera's facing when it is known, or the
	// manual value as a fallback. Camera-relative is what makes the square turn
	// with the view, so "up" is always where the player is looking.
	inline float effective_yaw(const state_t &s)
	{
		return s.cam_yaw_valid ? (s.cam_yaw + s.yaw) : s.yaw;
	}

	inline bool project(const state_t &s, const float world[2], float out[2])
	{
		const float rx0 = world[0] - s.camera[0];
		const float rz0 = world[1] - s.camera[2];
		const float yaw = effective_yaw(s);
		const float c = std::cos(yaw), sn = std::sin(yaw);
		const float rx = rx0 * c - rz0 * sn;
		const float rz = rx0 * sn + rz0 * c;
		const float scale = px_per_m(s);
		const float cx = s.x + s.size * 0.5f;
		const float cy = s.y + s.size * 0.5f;
		out[0] = cx + rx * scale;
		out[1] = cy - rz * scale;
		const float half = s.size * 0.5f;
		return std::fabs(out[0] - cx) <= half && std::fabs(out[1] - cy) <= half;
	}

	inline float movement_yaw(const mark &m, float radar_yaw)
	{
		const float dx = m.world[0] - m.prev_world[0];
		const float dz = m.world[1] - m.prev_world[1];
		if (dx * dx + dz * dz < 1e-6f)
			return 0.0f;   // stationary: no tick
		(void)radar_yaw;
		return std::atan2(dx, -dz) - effective_yaw(state());
	}
}

// ---------------------------------------------------------------------------
// Settings bridge: the menu owns the square (only unit with ImGui, so the only
// one that can drag), the draw unit reads it.
// ---------------------------------------------------------------------------
namespace chams_radar
{
	inline void sync_from_settings()
	{
		state_t &s = state();
		s.x = g_settings.radar_square_x;
		s.y = g_settings.radar_square_y;
		s.size = std::max(80.0f, g_settings.radar_square_px);
		s.area_m = std::max(5.0f, g_settings.radar_area_m);
	}
}
