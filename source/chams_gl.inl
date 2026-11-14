/*
 * chams_gl.inl -- chams rendering (OpenGL side)
 * created by hazu@alexainc
 *
 * Included from source/opengl/opengl.cpp, so the glad dispatch table ("gl")
 * and ReShade's thread local g_opengl_context are already available.
 *
 * ===========================================================================
 * WHAT THE TWO EFFECTS ARE
 * ===========================================================================
 *   glow   = the whole character filled with a colour (highlight, "3D" fill)
 *   stroke = a constant-width band on the OUTER side of the character's
 *            silhouette only (up / left / down / right), like a drawn outline.
 *            The body is NOT covered by it -- the game's own texture stays.
 *
 * ===========================================================================
 * HOW THE 2D OUTLINE (stroke) IS BUILT   -- v2.3 algorithm, restored
 * ===========================================================================
 *   1. the game's own draw happens first, so the body keeps its normal look
 *   2. MASK 0.0  write 0.0 into the depth buffer over the silhouette only
 *                (colour writes OFF, depth writes ON, GL_ALWAYS,
 *                 glDepthRange(0,0)) -> the character's own pixels now hold
 *                a depth that is NEARER than anything
 *   3. RING 8 copies of the same mesh, each shifted on screen by N pixels
 *                (right / left / up / down / 4 diagonals, done with
 *                glViewport so the band is measured in real pixels and also
 *                works for skinned meshes).  Depth test GL_LESS:
 *                   inside  -> stored 0.0 is nearer, copy rejected  (no fill!)
 *                   outside -> stored depth is the scene, copy wins (band)
 *                union of the copies - silhouette = a line of even width on
 *                the OUTSIDE of the character. Nothing on the body.
 *   4. MASK 1.0  same mask, value 1.0 (farther than everything)
 *   5. RING again with GL_GREATER: inside is protected by the 1.0 mask, so
 *                the copy only appears where something real is in front of
 *                the character -> the "through walls" outline.
 *                (This is why the mask is mandatory: v2.7-v2.9 dropped the
 *                 mask and the GL_GREATER ring painted the body instead -
 *                 the red / magenta 3D blobs.)
 *   6. DEPTH RESTORE  the mesh is drawn one more time with depth writes ON,
 *                GL_ALWAYS and the normal depth range, colour writes OFF,
 *                so the TRUE depth is written back over the 0.0 / 1.0 mask.
 *                This is the v3.0 fix for the "dark / mask line around the
 *                character": in v2.3 the mask values stayed in the buffer,
 *                so the game's own rim / outline passes and everything drawn
 *                after the body (gun, cloth, hair) lost the depth test and
 *                left a dark ring. Restoring the real depth gives all of
 *                them their normal depth back.
 *
 * ===========================================================================
 * SEEING THROUGH THE THROWABLE GLUE / ICE WALL  -- no percentage, one switch
 * ===========================================================================
 *   The wall is translucent and the game draws it AFTER the characters, so it
 *   blends on top of the chams. The only lever at character-draw time is
 *   depth. Two modes, both automatic:
 *
 *     normal      GL_GREATER claim, glDepthRange(0, 0.97)
 *                 -> the claim only lands where the character is already
 *                    covered by something (wall, car, another body), so the
 *                    cloth, the gun and the game's rim passes are untouched
 *     ice wall    GL_ALWAYS claim, glDepthRange(0, 0.72)
 *                 -> switches itself on while the game's ice-wall shader
 *                    (_FakeDepth_Ground) is on screen; that wall is drawn last
 *                    and would otherwise paint over the chams
 *
 *   The menu switch ("See enemies through walls") turns both on/off.
 *   No number to tune - a percentage was impossible to reason about in game.
 *
 * Blend for both effects is the recipe the original DLL used:
 *     glBlendFunc(GL_ONE, GL_CONSTANT_COLOR); glBlendColor(r, g, b, 1)
 * Nothing is queued and nothing is replayed after the frame, so the driver is
 * never asked to draw with released buffers (that was the v1.3 freeze).
 */

#pragma once

#include "chams.hpp"
#include "chams_trace.hpp"
#include "chams_capture.hpp"

#include "dll_log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace reshade::chams
{
	// ------------------------------------------------------------- helpers
	inline bool is_solid_primitive(GLenum mode)
	{
		return mode == GL_TRIANGLES || mode == GL_TRIANGLE_STRIP ||
		       mode == GL_TRIANGLE_FAN || mode == GL_QUADS || mode == GL_POLYGON;
	}

	// Optional lifecycle observations. The IDs below are GL object names in
	// this context/share group, not persistent identities. Call records alone
	// do not establish successful execution, GPU completion or frame boundaries.
	inline void capture_gl_lifecycle(const char *api, GLenum target = 0, GLuint object = 0,
		GLenum attachment = 0, GLint detail = 0, GLint detail2 = 0) noexcept
	{
		if (!capture::runtime_enabled() || g_opengl_context == nullptr) return;
		try {
			GLint draw = 0, read = 0, active = 0;
			if (gl.GetIntegerv) {
				gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
				gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
				gl.GetIntegerv(GL_ACTIVE_TEXTURE, &active);
			}
			char body[512];
			const int n = std::snprintf(body, sizeof(body),
				"\"api\":\"%s\",\"phase\":\"observed_call\",\"target\":%u,\"object\":%u,\"attachment\":%u,\"detail\":%d,\"detail2\":%d,\"draw_framebuffer\":%d,\"read_framebuffer\":%d,\"active_texture\":%d",
				api, static_cast<unsigned>(target), static_cast<unsigned>(object), static_cast<unsigned>(attachment), detail, detail2, draw, read, active);
			if (n <= 0 || static_cast<size_t>(n) >= sizeof(body)) { capture::runtime_record_loss(); return; }
			capture::runtime_gl_event(capture::native_context(), capture::native_surface(), body);
		} catch (...) { capture::runtime_record_loss(); }
	}

	// -------- character detection cache (uniform lookup is expensive) ----
	struct program_cache_entry { GLint program; int detect_mode; bool is_character; };
	struct program_cache { program_cache_entry e[32]; int count; };
	inline program_cache &pcache() { static thread_local program_cache c = {}; return c; }

	// -------- per program: uniform locations + cached matrices ----------
	// A matrix uniform in GLSL is either a real mat4 or - in hlslcc / ANGLE
	// generated shaders, which is what the emulator uses - an ARRAY OF 4 vec4
	// uniforms named "hlslcc_mtx4x4unity_ObjectToWorld[0]" .. "[3]".
	// v2.1 looked for "unity_ObjectToWorld"/"UNITY_MATRIX_V" only, found nothing,
	// so the distance was always -1 and the own-character filter never fired.
	// This version resolves every known spelling.
	struct matrix_loc
	{
		GLint loc[4] = { -1, -1, -1, -1 };  // 4 element locations when array style
		GLint plain  = -1;                  // single location when real mat4
		bool  array_style = false;

		bool valid() const { return array_style || plain != -1; }
	};

	struct program_info
	{
		GLint      program = 0;
		matrix_loc obj_to_world;   // hlslcc_mtx4x4unity_ObjectToWorld / unity_ObjectToWorld
		matrix_loc view;           // hlslcc_mtx4x4unity_MatrixV / UNITY_MATRIX_V
		matrix_loc view_proj;      // hlslcc_mtx4x4unity_MatrixVP / UNITY_MATRIX_VP
		GLint      loc_cam_pos = -1; // _WorldSpaceCameraPos (direct camera position)

		GLfloat    world[16] = {};
		GLfloat    view_m[16] = {};
		GLfloat    vp_m[16] = {};
		GLfloat    cam_uniform[3] = {};
		const char *dist_source = "-";
	};
	struct program_info_cache { program_info e[32]; int count; };
	inline program_info_cache &icache() { static thread_local program_info_cache c = {}; return c; }

	// viewport of the current frame (needed to convert pixels <-> NDC)
	struct viewport_cache { GLint x = 0, y = 0, w = 1920, h = 1080; };
	inline viewport_cache &vpcache() { static thread_local viewport_cache c; return c; }

	// Own-character estimate is a distance-only heuristic, not an ownership
	// signal. A nearby stationary enemy or accessory origin can be closer than
	// the actual player; object identity is not established by these values.
	// Consecutive plausible observations are required before exclusion is
	// attempted.
	struct own_track
	{
		float frame_min = 1e9f;   // closest character draw seen this frame (capture record only)
		// Distances of all character draws seen this frame (multipart histogram,
		// capped). The true invariant of the own model is its root cluster —
		// many draws at one near-constant distance per frame — not the single
		// closest (animated bone) origin that frame_min tracks.
		float frame_obs[64] = {};
		int   frame_obs_n   = 0;
		float cand      = -1.0f;  // candidate distance being confirmed
		float own_est   = -1.0f;  // learned distance of my own model (-1 = unknown)
		int   stable    = 0;      // consecutive frames the candidate held
		// Consecutive frames with no corroborated observation. A temporary
		// zero-observation frame (own avatar culled) must not destroy a valid
		// estimate: hide suspends (stale_frames > 0) and the very next
		// corroborated frame re-arms it. Only a streak longer than
		// kStaleFrameLimit (30, i.e. ~0.5 s) forgets for good (spectate safety).
		int   stale_frames = 0;
		// Displaced-cluster bookkeeping (Stage 3 FIX A). When the own avatar is
		// re-distanced beyond the 0.3 m candidate band (weapon lower/raise,
		// camera recomposure, walking), the frame modal keeps disagreeing with
		// the established reference and R2 refuses it. Correct for a one-frame
		// spike, wrong for a modal that *walks* with the avatar. rebase_modal
		// tracks the most recent displaced modal (walk-tolerant): each frame
		// within 0.10 m of the previous one extends the streak. Repeating for
		// kDisplacedClusterFrames consecutive frames, it is the own model in a
		// new distance band — re-anchor instead of drifting into the
		// kStaleFrameLimit forget-then-reacquire gap during which the avatar
		// draws visible. A foreign camped cluster is indistinguishable from a
		// re-distanced own model by distance alone; this path re-anchors sooner
		// than the post-forget reacquire that already happens today.
		float rebase_modal  = -1.0f;
		int   rebase_streak = 0;
		// Stage 4: combined uncorroborated-frame backstop (zero-sample frames
		// plus skip/wipe frames alike). A frame with ZERO comparable draws is
		// no information, not contradictory evidence, so it must not suspend
		// hiding (that produced a one-frame own-avatar chams flash after every
		// unrelated no-character frame); it still counts toward this backstop
		// so an unbroken uncorroborated streak forgets at the same
		// kStaleFrameLimit boundary as before (spectate/death safety, and the
		// bound is identical to the legacy stale_frames bound in all cases).
		int   no_info_streak = 0;
	};
	constexpr int kStaleFrameLimit = 30;
	constexpr int kDisplacedClusterFrames = 12;  // consecutive persistent displaced modal frames before the tracker re-anchors
	inline own_track &own() { static thread_local own_track o; return o; }

	// ======================================================== Stage 2 (F1)
	// Own-fork fingerprint binding (capture-validated; see
	// ff-investigation/OWNCHARACTER-FLICKER-ANALYSIS-2026-11-12.md §9).
	//
	// An own attachment (weapon/grip/accessory) is drawn every frame with a
	// dedicated fingerprint — (VBO, EBO, program, count, index offset, bound
	// texture) — strictly inside the root cluster's draw span, at a distance
	// that swings far outside est±0.2 m (two in-vivo captures: 1.80–3.45 m
	// around est 2.65–3.49 m; max |gap| 1.20 m; 0 of 5,634 draws beyond
	// 1.5 m). The per-draw distance predicate therefore cannot contain it.
	//
	// F1 learns that fingerprint online — GL object ids are per-session, so
	// nothing is hardcoded. A fingerprint becomes an own fork only when the
	// own root cluster is established (stable >= 30, estimate live) AND the
	// fingerprint was observed inside the root draw span — at least one root
	// draw before AND after it this frame — for kForkBindFrames (30)
	// consecutive corroborated frames with |dist - est| <= kForkLearnBand.
	// Once bound, the fork is hidden whenever the tracker is armed and the
	// own draw span has begun this frame, exactly like a root part.
	//
	// Enemy safety is by construction and by measurement. An enemy keeps a
	// dedicated fingerprint; binding additionally requires inter-leaving
	// between root draws for 30 consecutive corroborated frames. In the two
	// 2026-11-12 in-vivo captures and the earlier enemy-band capture, no
	// non-own fingerprint ever exceeded a 5-consecutive-frame streak under
	// these rules (own fork: 100–376). Hiding a bound fork additionally
	// requires this frame's own span to have begun (a root draw already seen
	// this frame) and dist <= the own-distance sanity limit, so a bound id
	// reused far from the avatar is never hidden.
	//
	// Never binds during cold start or in stale/unconfirmed tracker states.
	// Buffer/id reuse and outfit changes: fingerprints whose track reaches
	// kForkAbsentFrames (90) corroborated character-active frames without an
	// observation are forgotten, as are all bindings on context reset and on
	// hard tracker wipe (kStaleFrameLimit). Menu frames (no comparable
	// character draws) and uncorroborated frames neither bind, erode absence,
	// nor hide anything.
	constexpr int kForkBindFrames = 30;
	constexpr int kForkAbsentFrames = 90;
	constexpr int kForkMaxEntries = 4;
	constexpr int kForkMaxObs = 16;
	constexpr float kForkLearnBand = 1.5f;   // sanity for LEARNING only; never a hide predicate
	constexpr float kForkLearnMinDist = 1.0f;

	// Stage 8: per-part hysteresis. The acquire band (0.2 m, in
	// own_distance_excludes / own_fork_root_member) is narrower than the
	// bone-swing variance of the own model, so one part per frame typically
	// oscillates across the band edge (hide_own x3 / direct x1 flicker).
	// A root part hidden by the distance predicate latches its fingerprint;
	// while latched it stays hidden until its gap exceeds kHystReleaseBand.
	// Only fingerprints confirmed own (gap <= 0.2 while armed) latch, so a
	// foreign object is never given the wide band. Replay-validated on the
	// Naruto capture: release 0.75 m covers the observed root bone-swing
	// (fixed-gap max 0.719 m) and cuts single-draw flicker runs by ~99%.
	constexpr int kHystMaxEntries = 16;
	constexpr float kHystReleaseBand = 0.75f;  // release threshold (acquire band is 0.2)

	struct own_fork_fp {
		int program = 0, array_buffer = 0, element_buffer = 0, count = 0, texture = 0;
		uint64_t index_offset = 0;   // byte offset into the bound element buffer; 0 when non-indexed
	};
	struct own_fork_entry {
		own_fork_fp fp{};
		int in_run_streak = 0;       // consecutive corroborated bracketed frames seen
		int absent_streak = 0;       // consecutive corroborated character-active frames not seen
		unsigned last_counted_frame = 0xFFFFFFFFu;
		int seen_this_frame = 0;
		int bound = 0;
		int used = 0;
	};
	struct own_fork_obs {           // one non-root comparable character draw this frame
		own_fork_fp fp{};
		float dist = -1.0f, est = -1.0f;
		unsigned seq = 0;
	};
	struct own_hyst_entry {           // one latched root-part fingerprint (Stage 8)
		own_fork_fp fp{};
		int used = 0;
		int latched = 0;
	};
	struct own_fork_track {
		own_fork_entry entries[kForkMaxEntries];
		own_hyst_entry hyst[kHystMaxEntries];
		own_fork_obs obs[kForkMaxObs];
		int obs_count = 0, root_count = 0, char_draws = 0;
		unsigned draw_seq = 0;       // per-frame sequence of measured comparable character draws
		unsigned frame_serial = 0;   // incremented once per boundary finalise
		unsigned first_root_seq = 0, last_root_seq = 0;
	};
	inline own_fork_track &forks() { static thread_local own_fork_track t; return t; }

	inline bool own_fork_fp_equal(const own_fork_fp &a, const own_fork_fp &b)
	{
		return a.program == b.program && a.array_buffer == b.array_buffer &&
			a.element_buffer == b.element_buffer && a.count == b.count &&
			a.texture == b.texture && a.index_offset == b.index_offset;
	}
	inline own_fork_entry *own_fork_find(own_fork_track &t, const own_fork_fp &fp)
	{
		for (int i = 0; i < kForkMaxEntries; ++i)
			if (t.entries[i].used != 0 && own_fork_fp_equal(t.entries[i].fp, fp)) return &t.entries[i];
		return nullptr;
	}
	inline own_fork_entry *own_fork_find_free(own_fork_track &t)
	{
		for (int i = 0; i < kForkMaxEntries; ++i)
			if (t.entries[i].used == 0) return &t.entries[i];
		// Full: recycle the least-stale non-bound learning entry; keep bound entries.
		for (int i = 0; i < kForkMaxEntries; ++i)
			if (t.entries[i].bound == 0) { t.entries[i] = own_fork_entry{}; return &t.entries[i]; }
		return nullptr;
	}
	inline void own_fork_reset() { forks() = own_fork_track{}; }

	// The tracker must be established and corroborated; never during cold
	// start or in stale/unconfirmed tracker states. Applies to binding,
	// hiding and absence accounting alike.
	inline bool own_fork_eligible(const settings &s, const own_track &o)
	{
		return s.hide_own && o.own_est > 0.0f && o.stable >= 30 && o.stale_frames == 0;
	}

	// Geometric membership in the own root cluster this frame (same distance
	// predicate the exclusion uses; root members are hidden, or not, by the
	// distance predicate — F1 only tracks/frames them for the draw span).
	inline bool own_fork_root_member(const settings &s, const program_info &p, float distance, const own_track &o)
	{
		return std::strcmp(p.dist_source, "MATRIX_VP") != 0 && distance >= 0.0f &&
			o.own_est > 0.0f && o.stable >= 30 && o.stale_frames == 0 &&
			distance < s.own_distance && std::fabs(distance - o.own_est) <= 0.2f;
	}

	inline own_fork_fp own_fork_query_fp(GLint program, GLsizei count, uint64_t index_offset)
	{
		own_fork_fp fp{};
		fp.program = program; fp.count = count;
		GLint arr = 0, ele = 0, tex = 0;
		if (gl.GetIntegerv)
		{
			gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &arr);
			gl.GetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ele);
			gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
		}
		fp.array_buffer = arr; fp.element_buffer = ele; fp.texture = tex;
		fp.index_offset = (ele != 0) ? index_offset : 0;
		return fp;
	}

	// Stage 8 hysteresis latch helpers. The latch set lives in own_fork_track
	// so own_fork_reset() (tracker wipe / context change) clears it with the
	// fork bindings. No frame-boundary logic: the latch is managed per draw.
	inline own_hyst_entry *own_hyst_find(own_fork_track &t, const own_fork_fp &fp)
	{
		for (int i = 0; i < kHystMaxEntries; ++i)
			if (t.hyst[i].used != 0 && own_fork_fp_equal(t.hyst[i].fp, fp)) return &t.hyst[i];
		return nullptr;
	}
	inline void own_hyst_latch(own_fork_track &t, const own_fork_fp &fp)
	{
		own_hyst_entry *e = own_hyst_find(t, fp);
		if (e == nullptr)
		{
			for (int i = 0; i < kHystMaxEntries; ++i)
				if (t.hyst[i].used == 0) { e = &t.hyst[i]; break; }
			if (e == nullptr) return;  // full: skip latching (safe fallback: part flickers)
			e->fp = fp; e->used = 1;
		}
		e->latched = 1;
	}
	inline bool own_hyst_latched(const own_fork_track &t, const own_fork_fp &fp)
	{
		const own_hyst_entry *e = nullptr;
		for (int i = 0; i < kHystMaxEntries; ++i)
			if (t.hyst[i].used != 0 && t.hyst[i].latched != 0 && own_fork_fp_equal(t.hyst[i].fp, fp)) { e = &t.hyst[i]; break; }
		return e != nullptr;
	}
	inline void own_hyst_release(own_fork_track &t, const own_fork_fp &fp)
	{
		own_hyst_entry *e = own_hyst_find(t, fp);
		if (e != nullptr) e->latched = 0;
	}
	// Hysteresis hide gate: same arming conditions as own_distance_excludes
	// but with the release band. Only latched (previously-confirmed-own)
	// parts reach here; the caller checks the latch.
	inline bool own_hyst_gate(const settings &s, const program_info &p, float distance, const own_track &o)
	{
		return s.hide_own && std::strcmp(p.dist_source, "MATRIX_VP") != 0 && distance >= 0.0f &&
			o.own_est > 0.0f && o.stable >= 30 && o.stale_frames == 0 &&
			distance < s.own_distance && std::fabs(distance - o.own_est) <= kHystReleaseBand;
	}

	// Hide gate for a bound fork: armed tracker AND the own draw span having
	// begun this frame (a root draw already seen) AND distance inside the
	// own-distance sanity limit (deflects bound-id reuse far from the
	// avatar). Never hides on distance alone; never hides before the span
	// begins; RC-3 (whole avatar out of band) and stale frames correctly
	// leave the fork visible.
	inline bool own_fork_hide_gate(const settings &s, float distance, const own_track &o, const own_fork_track &t)
	{
		if (!own_fork_eligible(s, o)) return false;
		if (t.root_count <= 0) return false;             // own span has not begun this frame
		return std::isfinite(distance) && distance > 0.0f && distance <= s.own_distance;
	}
	// Per-draw measurement for one comparable character draw. Returns true when
	// the draw is a bound own fork that must be hidden now (caller suppresses
	// the chams passes exactly as it does for the distance predicate).
	// Test/debug surface for the hide gate + bound-membership check. The
	// production path inlines the same decision inside own_fork_note_draw.
	inline bool own_fork_should_hide(const settings &s, float distance, const own_track &o,
			const own_fork_track &t, const own_fork_fp &fp)
	{
		if (!own_fork_hide_gate(s, distance, o, t)) return false;
		for (int i = 0; i < kForkMaxEntries; ++i)
			if (t.entries[i].used != 0 && t.entries[i].bound != 0 && own_fork_fp_equal(t.entries[i].fp, fp)) return true;
		return false;
	}
	inline bool own_fork_note_draw(const settings &s, const program_info &p, float distance,
		const own_track &o, GLint program, GLsizei count, uint64_t index_offset)
	{
		own_fork_track &t = forks();
		if (o.stale_frames != 0)
			return false;   // uncorroborated frame: F1 inert, whole avatar pops (R1 semantics)
		const unsigned seq = t.draw_seq++;
		++t.char_draws;
		if (own_fork_root_member(s, p, distance, o))
		{
			if (t.root_count == 0 || seq < t.first_root_seq) t.first_root_seq = seq;
			if (t.root_count == 0 || seq > t.last_root_seq) t.last_root_seq = seq;
			++t.root_count;
			// Stage 8 hysteresis: latch this root part. It is hidden by the
			// distance predicate now; the latch keeps it hidden if its bone
			// origin drifts just outside the acquire band.
			own_hyst_latch(t, own_fork_query_fp(program, count, index_offset));
			return false;   // root members are hidden, or not, by the distance predicate
		}
		if (!std::isfinite(distance) || distance <= 0.0f)
			return false;
		const own_fork_fp fp = own_fork_query_fp(program, count, index_offset);
		// Stage 8 hysteresis: a latched root part that drifted outside the
		// acquire band stays hidden until its gap exceeds the release band.
		// Unlatch only on a genuine large gap; disarmed frames (stale etc.)
		// stay latched and simply do not hide (R1 grace).
		if (own_hyst_latched(t, fp))
		{
			if (std::fabs(distance - o.own_est) > kHystReleaseBand)
				own_hyst_release(t, fp);
			else if (own_hyst_gate(s, p, distance, o))
				return true;
		}
		// A known fingerprint counts as seen even when bound or not drawable
		// right now (span not begun / distance out of sanity): absence means
		// "vanished from the frame", not "not hidden this frame".
		own_fork_entry *unbound = nullptr;
		for (int i = 0; i < kForkMaxEntries; ++i)
		{
			own_fork_entry &e = t.entries[i];
			if (e.used == 0 || !own_fork_fp_equal(e.fp, fp)) continue;
			if (e.bound != 0)
			{
				e.seen_this_frame = 1;
				e.absent_streak = 0;
				if (own_fork_hide_gate(s, distance, o, t))
					return true;
				return false;   // bound but not drawable now (span not begun / out of cap): counted as seen
			}
			unbound = &e;
			break;   // unbound learning entry: fall through to obs recording
		}
		if (t.obs_count < kForkMaxObs)
		{
			own_fork_obs &ob = t.obs[t.obs_count++];
			ob.fp = fp; ob.dist = distance; ob.est = o.own_est; ob.seq = seq;
		}
		else if (unbound != nullptr)
		{
			// The observation buffer is full, so this tracked candidate cannot be
			// recorded this frame. Do not let the dropped observation silently
			// preserve an in-run streak: count the fingerprint as seen (no absence
			// accrues), break the streak, and never let it count toward (much less
			// complete) a binding run.
			unbound->seen_this_frame = 1;
			unbound->in_run_streak = 0;
			unbound->absent_streak = 0;
		}
		return false;
	}

	// Boundary finalise for F1. Runs once per frame at the same point the
	// tracker closes the frame (reset_frame_caches). Decides — with the
	// corroboration outcome of the closing frame — whether this frame
	// advances binding streaks, breaks them, or leaves them untouched.
	// Menu frames (no comparable draws) and uncorroborated frames erode
	// nothing: absence is only counted on corroborated character-active
	// frames, and streaks only advance on corroborated frames.
	inline void own_fork_frame_boundary()
	{
		own_fork_track &t = forks();
		const own_track &o = own();
		const settings &s = g_settings;
		// Post-update corroboration of the frame that just closed: hiding was
		// armed for this frame iff the tracker learned on it.
		const bool corroborated = o.stale_frames == 0;
		const bool active = t.char_draws > 0;
		const bool eligible = own_fork_eligible(s, o);
		++t.frame_serial;
		for (int i = 0; i < t.obs_count; ++i)
		{
			const own_fork_obs &ob = t.obs[i];
			own_fork_entry *e = own_fork_find(t, ob.fp);
			const bool sane = std::isfinite(ob.dist) && std::isfinite(ob.est) &&
				std::fabs(ob.dist - ob.est) <= kForkLearnBand && ob.dist >= kForkLearnMinDist && ob.est > 0.0f;
			const bool bracketed = t.root_count > 0 && ob.seq > t.first_root_seq && ob.seq < t.last_root_seq;
			if (eligible && bracketed && sane)
			{
				if (e == nullptr)
				{
					e = own_fork_find_free(t);
					if (e != nullptr) { *e = own_fork_entry{}; e->fp = ob.fp; e->used = 1; }
				}
				if (e != nullptr)
				{
					if (e->last_counted_frame != t.frame_serial) {
						e->last_counted_frame = t.frame_serial;
						if (e->in_run_streak < kForkBindFrames) ++e->in_run_streak;
						if (e->in_run_streak >= kForkBindFrames) e->bound = 1;
					}
					e->absent_streak = 0;
					e->seen_this_frame = 1;
				}
			}
			else if (eligible)
			{
				// Observed in a corroborated frame but not as a bracketed,
				// plausible attachment: consecutiveness breaks. Untracked
				// fingerprints seen out-of-window are ignored entirely.
				if (e != nullptr) { e->in_run_streak = 0; e->absent_streak = 0; e->seen_this_frame = 1; }
			}
			else if (e != nullptr)
			{
				e->seen_this_frame = 1;   // uncorroborated frame: seen, streaks untouched
			}
		}
		for (int i = 0; i < kForkMaxEntries; ++i)
		{
			own_fork_entry &e = t.entries[i];
			if (e.used == 0) continue;
			if (e.seen_this_frame != 0) { e.seen_this_frame = 0; continue; }
			if (active && corroborated)
			{
				++e.absent_streak;
				if (e.absent_streak > kForkAbsentFrames) e = own_fork_entry{};   // unbind; slot recycled
			}
		}
		t.obs_count = 0; t.root_count = 0; t.char_draws = 0; t.draw_seq = 0;
		t.first_root_seq = 0; t.last_root_seq = 0;
	}

	// Record one comparable character-draw distance for this frame. O(1),
	// drop-on-full; the frame minimum is still tracked separately.
	inline void note_character_distance(float dist)
	{
		if (!std::isfinite(dist) || dist <= 0.0f)
			return;
		own_track &o = own();
		if (o.frame_obs_n < static_cast<int>(sizeof(o.frame_obs) / sizeof(o.frame_obs[0])))
			o.frame_obs[o.frame_obs_n++] = dist;
	}

	// Single definition of the legacy per-draw decision. Kept separate so
	// capture-derived tests can expose its known limitations without claiming
	// that a distance match establishes ownership.
	inline bool own_distance_excludes(const settings &s, const program_info &p, float distance, const own_track &o)
	{
		return s.hide_own && std::strcmp(p.dist_source, "MATRIX_VP") != 0 && distance >= 0.0f &&
			o.own_est > 0.0f && o.stable >= 30 && o.stale_frames == 0 && distance < s.own_distance &&
			std::fabs(distance - o.own_est) <= 0.2f;
	}

	// Pick this frame's own-character observation from the multipart histogram.
	// The own model is drawn as many parts per frame whose origins cluster at a
	// near-constant distance (the root), while the frame minimum is only the
	// nearest animated bone origin. A gun-switch / ADS transient swings that
	// bone toward the camera and must not destroy the sequence.
	//
	// Returns the modal distance (>=1.5m, <=5.0m) with >=3 votes; with fewer
	// than 3 plausible samples the frame is *corroborated* only when one of
	// those samples agrees with the established candidate/estimate (missing
	// own-part draws — e.g. a weapon fork hidden for a frame — otherwise an
	// enemy at 3-5m could rebase the tracker and wipe its progress); without
	// an established reference (cold start) the nearest plausible sample is
	// used as before; with 3+ plausible samples but no cluster the frame is
	// skipped; if samples exist but all are <1.5m the frame is skipped
	// (near-camera transient) instead of wiping the estimate. Zero comparable
	// draws at all keeps the legacy forget behaviour.
	//
	// Stage 3 FIX A: a displaced modal (>0.3 m from the reference, >=3 votes)
	// that persists for kDisplacedClusterFrames consecutive frames — each
	// frame's modal within 0.10 m of the previous one (walk-tolerant) — is
	// the own model re-distanced (weapon lower/raise, camera recomposure,
	// walking), not a one-frame enemy spike. Such a frame returns the modal
	// with *rebase=true so the tracker re-anchors without restarting
	// stability. Anything else displaced keeps legacy R2 semantics (skip;
	// the estimate is forgotten after the kStaleFrameLimit streak).
	inline float pick_frame_observation(own_track &o, bool *skip, bool *wipe, int *votes_out, bool *rebase)
	{
		*skip = false; *wipe = false; *votes_out = 0; *rebase = false;
		const float reference = o.own_est > 0.0f ? o.own_est : o.cand;
		int plausible = 0, best_votes = 0;
		float min_plausible = 1e9f, best_modal = -1.0f, closest_plausible = 1e9f;
		bool any_near = false;
		for (int i = 0; i < o.frame_obs_n; ++i)
		{
			const float d = o.frame_obs[i];
			if (d < 1.5f) { any_near = true; continue; }
			if (d > 5.0f) continue;
			++plausible;
			if (d < min_plausible) min_plausible = d;
			if (reference > 0.0f && std::fabs(d - reference) < std::fabs(closest_plausible - reference))
				closest_plausible = d;
			int votes = 0;
			for (int j = 0; j < o.frame_obs_n; ++j)
				if (std::fabs(o.frame_obs[j] - d) <= 0.05f) ++votes;
			if (votes > best_votes) { best_votes = votes; best_modal = d; }
		}
		// Legacy single-sample path: frame_min may be set without samples.
		const bool legacy_single = o.frame_obs_n == 0 && o.frame_min < 1e8f;
		if (legacy_single)
		{
			min_plausible = o.frame_min;
			best_modal = o.frame_min;
			best_votes = 1;
			any_near = o.frame_min < 1.5f;
			if (o.frame_min >= 1.5f && o.frame_min <= 5.0f)
			{
				plausible = 1;
				closest_plausible = o.frame_min;
			}
		}

		if (plausible > 0)
		{
			if (plausible < 3)
			{
				o.rebase_modal = -1.0f; o.rebase_streak = 0;   // persistence broken
				if (reference > 0.0f)
				{
					// Sparse frame with an established tracker: learn only from
					// a sample that agrees with it. A distant lone sample (enemy,
					// or a detached fork) proves nothing and must not rebase the
					// candidate — that is the missing-draw / weapon-hide dropout.
					if (std::fabs(closest_plausible - reference) <= 0.3f)
					{
						*votes_out = best_votes;
						return closest_plausible;
					}
					*skip = true;          // corroboration missing: keep the estimate
					return -1.0f;
				}
				*votes_out = best_votes;
				return min_plausible;      // cold start: nearest plausible (legacy)
			}
			if (best_votes >= 3)
			{
				// R2 (established-reference corroboration): a foreign multi-vote
				// majority that disagrees with an established candidate/estimate is
				// an enemy cluster, not the own model. Learning it rebased the
				// tracker to a nearby enemy (post-fix capture: candidate_changed
				// at 2.43m / 2.15m / 2.40m). Skip the frame instead of absorbing it.
				if (reference > 0.0f && std::fabs(best_modal - reference) > 0.3f)
				{
					// R2 keeps a lone displaced majority out (it is an enemy
					// until proven otherwise), but a modal that *walks* with the
					// avatar — each frame within 0.10 m of the previous modal, for
					// kDisplacedClusterFrames consecutive frames — is the own model
					// re-distanced. Re-anchor instead of drifting into the
					// forget-then-reacquire visibility gap. The streak follows a
					// monotonic walk; alternating clusters break it.
					if (o.rebase_modal > 0.0f && std::fabs(best_modal - o.rebase_modal) <= 0.10f)
						++o.rebase_streak;
					else
						o.rebase_streak = 1;
					o.rebase_modal = best_modal;
					if (o.rebase_streak >= kDisplacedClusterFrames)
					{
						*rebase = true;
						*votes_out = best_votes;
						return best_modal;
					}
					*skip = true;
					return -1.0f;
				}
				o.rebase_modal = -1.0f; o.rebase_streak = 0;   // near the reference: normal cluster
				*votes_out = best_votes;
				return best_modal;          // persistent root cluster
			}
			o.rebase_modal = -1.0f; o.rebase_streak = 0;      // ambiguous: persistence broken
			*skip = true;                  // several candidates, no cluster: learn nothing
			return -1.0f;
		}
		// Stage 4 principle applied to the displaced-cluster walk: a pure
		// zero-observation frame (no comparable draws at all) is no
		// information, not contradictory evidence — keep the walk so an
		// unrelated culled/menu frame mid-re-distance does not delay the
		// re-anchor (stage4cap: rebase observed at 8526 instead of 8515).
		// Frames that saw samples but none plausible (near-camera or all too
		// far) still break persistence, exactly as before.
		const bool zero_observation = (o.frame_obs_n == 0 && o.frame_min >= 1e8f);
		if (!zero_observation) { o.rebase_modal = -1.0f; o.rebase_streak = 0; }   // no plausible samples: persistence broken
		if (legacy_single ? (o.frame_min < 1.5f) : any_near)
		{
			// Near-camera transient (bone swing). Forgetting here destroys the
			// learned estimate; keep it untouched instead.
			*skip = true;
			return -1.0f;
		}
		if (!legacy_single && o.frame_obs_n == 0 && o.frame_min >= 1e8f)
		{
			// Nothing drawn this frame: forget the estimate (a nearby enemy in
			// the next frame must not inherit it).
			*wipe = true;
			return -1.0f;
		}
		// Samples existed but every one is too far: same forget as legacy.
		*wipe = true;
		return -1.0f;
	}

	inline void update_own_estimate()
	{
		own_track &o = own();
		const bool tracking = capture::runtime_enabled();
		const own_track before = tracking ? o : own_track{};
		bool skip = false, wipe = false, rebase = false;
		int mode_votes = 0;
		const float observation = pick_frame_observation(o, &skip, &wipe, &mode_votes, &rebase);
		if (rebase)
		{
			// Stage 3 FIX A: a persistent displaced modal (a walk, not a spike)
			// has now held for kDisplacedClusterFrames consecutive frames —
			// re-anchor to the own model's new distance without restarting
			// stability. The avatar is drawn every frame; this is not a new
			// candidate.
			o.cand = observation;
			o.own_est = observation;
			o.stale_frames = 0;
			o.no_info_streak = 0;
			o.rebase_modal = -1.0f;
			o.rebase_streak = 0;
		}
		else if (!skip && !wipe)
		{
			// Only learn from plausible third-person distances (1.5m - 5m).
			// Enemy distances outside this range are ignored so they cannot
			// corrupt the own-character estimate.
			if (o.cand < 0.0f || std::fabs(observation - o.cand) > 0.3f)
			{
				o.cand   = observation;
				o.stable = 1;
			}
			else
			{
				o.cand   = o.cand * 0.7f + observation * 0.3f;
				o.stable = (o.stable < 1000) ? o.stable + 1 : 1000;
			}
			// Only accept consecutive plausible observations. This distance is
			// still a heuristic and cannot establish player ownership.
			if (o.stable >= 30)
				o.own_est = o.cand;
			o.stale_frames = 0;
			o.no_info_streak = 0;
		}
		else
		{
			// R1 (stale grace): a frame that cannot corroborate the reference
			// (skip) or saw contradictory samples (out_of_range wipe) suspends
			// exclusion and extends the stale streak — a brief own-avatar cull
			// must not destroy a valid estimate, and the very next corroborated
			// frame re-arms hiding. Zero-sample frames (2026-11-13 Stage 4) do
			// NOT suspend exclusion; see below. An unbroken uncorroborated
			// streak of either kind longer than kStaleFrameLimit forgets for
			// good.
			const bool had_reference = o.own_est > 0.0f || o.cand > 0.0f;
			// Stage 4: a frame with zero comparable draws (nothing reached the
			// distance code at all — e.g. a cutscene/respawn/loading frame) is
			// no information, not contradictory evidence. It must not suspend
			// hiding: suspending here flipped the whole own avatar (root and
			// fork) and any in-band character to the chams passes for exactly
			// one frame, producing the brief low-frequency flicker. Frames that
			// saw draws which cannot corroborate the reference (out_of_range,
			// uncorroborated/ambiguous/near-camera skip) keep R1 semantics —
			// they suspend hiding as before. Both kinds advance the combined
			// backstop so an unbroken uncorroborated streak still forgets at
			// the kStaleFrameLimit boundary (identical bound to the legacy
			// stale_frames bound in all cases).
			const bool zero_observation = (o.frame_obs_n == 0 && o.frame_min >= 1e8f);
			++o.no_info_streak;
			if (!zero_observation)
				o.stale_frames += 1;
			if (!had_reference || o.no_info_streak > kStaleFrameLimit || o.stale_frames > kStaleFrameLimit)
			{
				o.cand    = -1.0f;
				o.stable  = 0;
				o.own_est = -1.0f;
				o.stale_frames = 0;
				o.no_info_streak = 0;
				own_fork_reset();   // a forgotten reference never keeps stale fingerprints
				o.rebase_modal = -1.0f;
				o.rebase_streak = 0;
			}
			// else: keep candidate/estimate/stability; hiding is suspended while
			// stale_frames > 0 (see own_distance_excludes).
		}
		// skip: a near-camera transient, an uncorroborated or an ambiguous
		// frame was observed; candidate/estimate/stability survive (stale path).
		if (tracking) {
			const char *reason = rebase ? "displaced_cluster_rebase" :
				skip ? "outlier_skipped" :
				wipe ? ((before.frame_min >= 1e8f && before.frame_obs_n == 0) ? "no_observation" : "out_of_range") :
				(before.cand < 0 || std::fabs(observation - before.cand) > 0.3f) ? "candidate_changed" :
				(before.stable < 30 && o.stable >= 30) ? "acquired" : "candidate_continued";
			try { std::string line = std::string("\"reason\":\"") + reason + "\",\"legacy_global_frame\":" + std::to_string(g_frame_id) +
				",\"frame_min\":" + capture::json_number(before.frame_min) + ",\"before_candidate\":" + capture::json_number(before.cand) +
				",\"before_estimate\":" + capture::json_number(before.own_est) + ",\"before_stable\":" + std::to_string(before.stable) +
				",\"candidate\":" + capture::json_number(o.cand) + ",\"estimate\":" + capture::json_number(o.own_est) + ",\"stable\":" + std::to_string(o.stable) +
				",\"stale_frames\":" + std::to_string(o.stale_frames) +
				",\"no_info_streak\":" + std::to_string(o.no_info_streak) +
				",\"mode\":" + capture::json_number(skip || wipe ? -1.0f : observation) +
				",\"mode_votes\":" + std::to_string(skip || wipe ? 0 : mode_votes);
				capture::runtime_inflight_event(capture::native_context(), "own_tracker", line,
					capture::runtime_inflight_surface(capture::native_surface()));
			} catch (...) { /* diagnostic must not terminate the renderer */ }
		}
		o.frame_min = 1e9f;
		o.frame_obs_n = 0;
	}

	inline void reset_frame_caches()
	{
		update_own_estimate();
		own_fork_frame_boundary();

		pcache().count = 0;
		icache().count = 0;

		GLint vp[4] = { 0, 0, 1920, 1080 };
		if (gl.GetIntegerv) gl.GetIntegerv(GL_VIEWPORT, vp);
		if (vp[2] > 0 && vp[3] > 0) {
			vpcache().x = vp[0]; vpcache().y = vp[1];
			vpcache().w = vp[2]; vpcache().h = vp[3];
		}
	}

	inline void touch_frame()
	{
		// ReShade can reuse one device wrapper for several native WGL contexts
		// in a share group, so the wrapper pointer is not a context identity.
		// Isolate caches and the legacy estimate whenever either changes. A
		// migration to another thread deliberately starts fresh. Native handle
		// reuse without an observed draw between lifetimes remains unhandled.
		static thread_local const void *last_device = nullptr;
		static thread_local uintptr_t last_native_context = 0;
		static thread_local unsigned last_frame = 0xFFFFFFFFu;
		const uintptr_t native_context = capture::native_context();
		const unsigned frame = g_frame_id.load(std::memory_order_relaxed);
		if (last_device != g_opengl_context || last_native_context != native_context)
		{
			last_device = g_opengl_context;
			last_native_context = native_context;
			own() = own_track{};
			own_fork_reset();
			pcache().count = 0;
			icache().count = 0;
			last_frame = frame;
			GLint vp[4] = { 0, 0, 1920, 1080 };
			if (gl.GetIntegerv) gl.GetIntegerv(GL_VIEWPORT, vp);
			if (vp[2] > 0 && vp[3] > 0) vpcache() = { vp[0], vp[1], vp[2], vp[3] };
		}
		else if (last_frame != frame) { last_frame = frame; reset_frame_caches(); }
	}

	// ---------------------------------------------------- detection tokens
	// From the REAL game files (asset pack 1.132.1, 464 BRMobile shaders):
	// 63 avatar shader families (Skin, XJ_Skin, Hair, Cloth, Cloth_2uv,
	// ClothSpecial, Cloth_FlowOutline, Cloth_FlowOutline_Transparent, Helmet,
	// Glass, Erosion, PBR_Avatar, DefaultAvatar*, MatCap_Cloth, Toon_AuraOutline,
	// UGC/MiniAvatar*, CutScene/* ...). They share one thing the world shaders
	// do not have: a small set of avatar-only uniforms. Cross-checked against
	// our own runtime dumps, these are the ones that appear ONLY on character
	// programs:
	//     _CharaLightIntensity   (the original DLL's rule)
	//     _BackLightDir / _BackLightColor
	//     _SkinBaseColor         (skin / cloth base tint)
	//     _DiffIntensity         (diffuse ramp strength)
	//     _SpecularmapGlossA     (spec + gloss packed in one map)
	//     _StretchRatio          (cloth / hair stretch)
	//     _ColorEffectParam      (cloth colour effects)
	// One match is enough - that is the "wide" rule, and it is why a new skin
	// or a new cloth shader is picked up without waiting for an update.
	static const char *const k_chara_tokens[] = {
		"_CharaLightIntensity",
		"_BackLightDir", "_BackLightColor",
		"_SkinBaseColor", "_DiffIntensity",
		"_SpecularmapGlossA", "_StretchRatio", "_ColorEffectParam"
	};

	// Skinned meshes always carry one of these; nothing static does. They are
	// the only way to catch a bundle whose shader family we have never seen.
	static const char *const k_skin_uniforms[] = {
		"unity_BoneMatrices",  "hlslcc_mtx4x4unity_BoneMatrices",
		"_BoneMatrices",       "hlslcc_mtx4x4_BoneMatrices",
		"boneMatrices",        "_SkinMatrices", "hlslcc_mtx4x4_SkinMatrices",
		"_SkinMatrix",         "hlslcc_mtx4x4_SkinMatrix",
		"_SkinBones",          "_BoneTransforms", "unity_BoneWeights"
	};

	// Cel-shaded / toon character family (Stage 3 FIX B). Anime-style bundles
	// render avatars with cel shading instead of the avatar tokens above: a
	// cel ramp threshold (+ bright/shade ramp) for the body pass and an
	// inverted-hull outline pass. Neither carries _CharaLightIntensity nor a
	// bone-matrix uniform, so every character draw of such a bundle stops at
	// this classifier today. One cel token is enough, mirroring the wide
	// rule; these are shader-family conventions, never a specific bundle,
	// texture, mesh or VBO. The MToon entries cover Unity's official toon
	// shader family (_ShadeThreshold / _ShadeColor / _ShadeShift).
	static const char *const k_cel_tokens[] = {
		"_ShadowThreshold", "_BrightColor",                  // cel ramp body passes
		"_ShadeThreshold", "_ShadeColor", "_ShadeShift",    // Unity MToon-style bodies
		"_OutLineColor", "_OutLineWidth", "_OutLineZOffset" // inverted-hull outline passes
	};

	inline bool program_has_cel_token(GLint program)
	{
		if (gl.GetUniformLocation == nullptr)
			return false;
		for (const char *name : k_cel_tokens)
			if (gl.GetUniformLocation(static_cast<GLuint>(program), name) != -1)
				return true;
		return false;
	}

	inline bool program_has_skin_uniform(GLint program)
	{
		if (gl.GetUniformLocation == nullptr)
			return false;
		for (const char *name : k_skin_uniforms)
			if (gl.GetUniformLocation(static_cast<GLuint>(program), name) != -1)
				return true;
		return false;
	}

	// which avatar token this program carries (for the dump / for the log)
	inline const char *character_token(GLint program)
	{
		if (gl.GetUniformLocation == nullptr)
			return "-";
		for (const char *name : k_chara_tokens)
			if (gl.GetUniformLocation(static_cast<GLuint>(program), name) != -1)
				return name;
		return "-";
	}

	inline bool program_is_character(GLint program)
	{
		if (program == 0 || gl.GetUniformLocation == nullptr)
			return false;

		program_cache &c = pcache();
		for (int i = 0; i < c.count; ++i)
			if (c.e[i].program == program && c.e[i].detect_mode == g_settings.detect_mode)
				return c.e[i].is_character;

		const bool has_chara = gl.GetUniformLocation(static_cast<GLuint>(program),
		                                             k_character_uniform) != -1;

		bool has_wide = has_chara;
		if (!has_wide)
			has_wide = character_token(program)[0] != '-';
		if (!has_wide)
			has_wide = program_has_cel_token(program);   // Stage 3 FIX B: cel/toon family

		bool has = false;
		switch (g_settings.detect_mode)
		{
		case 0:  // strict: original DLL match + cel/toon family (Stage 7: Naruto/2D cel passes carry no chara token)
			has = has_chara || program_has_cel_token(program);
			break;
		case 2:  // skinned: any bone matrix uniform (unknown skins / bundles)
			has = program_has_skin_uniform(program);
			break;
		case 3:  // all
			has = has_wide || program_has_skin_uniform(program);
			break;
		case 1:  // wide (default): the avatar token set above
		default:
			has = has_wide;
			break;
		}

		if (c.count < 32) {
			c.e[c.count].program = program;
			c.e[c.count].detect_mode = g_settings.detect_mode;
			c.e[c.count].is_character = has;
			++c.count;
		}
		return has;
	}

	// try every spelling of a matrix uniform, array-of-vec4 style first
	inline matrix_loc resolve_matrix(GLint program, const char *const *names, int count)
	{
		matrix_loc m;
		for (int n = 0; n < count; ++n)
		{
			char buf[128];
			GLint locs[4] = { -1, -1, -1, -1 };
			int found = 0;
			for (int i = 0; i < 4; ++i)
			{
				std::snprintf(buf, sizeof(buf), "%s[%d]", names[n], i);
				locs[i] = gl.GetUniformLocation(static_cast<GLuint>(program), buf);
				if (locs[i] != -1) ++found;
			}
			if (found == 4)
			{
				for (int i = 0; i < 4; ++i) m.loc[i] = locs[i];
				m.array_style = true;
				return m;
			}

			const GLint plain = gl.GetUniformLocation(static_cast<GLuint>(program), names[n]);
			if (plain != -1)
			{
				m.plain = plain;
				m.array_style = false;
				return m;
			}
		}
		return m;
	}

	inline void read_matrix(GLint program, const matrix_loc &loc, GLfloat out[16])
	{
		if (!loc.valid() || gl.GetUniformfv == nullptr)
			return;
		if (loc.array_style)
		{
			for (int i = 0; i < 4; ++i)
				gl.GetUniformfv(static_cast<GLuint>(program), loc.loc[i], &out[i * 4]);
		}
		else
		{
			gl.GetUniformfv(static_cast<GLuint>(program), loc.plain, out);
		}
	}

	inline program_info *get_program_info(GLint program)
	{
		if (program == 0 || gl.GetUniformLocation == nullptr)
			return nullptr;

		program_info_cache &c = icache();
		for (int i = 0; i < c.count; ++i)
			if (c.e[i].program == program)
				return &c.e[i];
		if (c.count >= 32)
			return nullptr;

		program_info &p = c.e[c.count++];
		// Slots are reused after each frame; do not carry matrices or camera
		// values from a different program into this program's fallback paths.
		p = program_info{};
		p.program = program;

		static const char *const otw_names[] = {
			"hlslcc_mtx4x4unity_ObjectToWorld", "unity_ObjectToWorld",
			"_ObjectToWorld", "unity_ObjectToWorldArray"
		};
		static const char *const view_names[] = {
			"hlslcc_mtx4x4unity_MatrixV", "UNITY_MATRIX_V", "unity_MatrixV"
		};
		static const char *const vp_names[] = {
			"hlslcc_mtx4x4unity_MatrixVP", "UNITY_MATRIX_VP", "unity_MatrixVP"
		};

		p.obj_to_world = resolve_matrix(program, otw_names, 4);
		p.view         = resolve_matrix(program, view_names, 3);
		p.view_proj    = resolve_matrix(program, vp_names, 3);
		p.loc_cam_pos  = gl.GetUniformLocation(static_cast<GLuint>(program), "_WorldSpaceCameraPos");

		return &p;
	}

	// re-read the matrices the game uploaded for this draw
	inline void read_matrices(program_info &p)
	{
		if (gl.GetUniformfv == nullptr)
			return;
		read_matrix(p.program, p.obj_to_world, p.world);
		read_matrix(p.program, p.view, p.view_m);
		read_matrix(p.program, p.view_proj, p.vp_m);
		if (p.loc_cam_pos != -1)
			gl.GetUniformfv(static_cast<GLuint>(p.program), p.loc_cam_pos, p.cam_uniform);
	}

	// camera position from the view matrix:  cam = -(R^T * t)
	inline bool camera_position(const GLfloat view[16], float out[3])
	{
		out[0] = -(view[0] * view[12] + view[1] * view[13] + view[2]  * view[14]);
		out[1] = -(view[4] * view[12] + view[5] * view[13] + view[6]  * view[14]);
		out[2] = -(view[8] * view[12] + view[9] * view[13] + view[10] * view[14]);
		return !(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f);
	}

	// camera position for the dump / for the fallback distance test
	inline bool camera_of(const program_info &p, float out[3], bool *from_uniform = nullptr)
	{
		if (from_uniform != nullptr) *from_uniform = false;
		if (p.loc_cam_pos != -1 &&
		    !(p.cam_uniform[0] == 0.0f && p.cam_uniform[1] == 0.0f && p.cam_uniform[2] == 0.0f))
		{
			out[0] = p.cam_uniform[0]; out[1] = p.cam_uniform[1]; out[2] = p.cam_uniform[2];
			if (from_uniform != nullptr) *from_uniform = true;
			return true;
		}
		return p.view.valid() && camera_position(p.view_m, out);
	}

	// Recover the finite perspective centre from VP: the centre maps to
	// clip x = y = w = 0. Orthographic, singular and ill-conditioned matrices
	// do not define a usable finite centre. Use double intermediates because
	// world translations can be large relative to camera-to-object distance.
	inline bool perspective_camera(const GLfloat m[16], float out[3])
	{
		const double a[3] = { m[0], m[4], m[8] };
		const double b[3] = { m[1], m[5], m[9] };
		const double c[3] = { m[3], m[7], m[11] };
		for (int i = 0; i < 16; ++i) if (!std::isfinite(m[i])) return false;
		const double bc[3] = { b[1]*c[2]-b[2]*c[1], b[2]*c[0]-b[0]*c[2], b[0]*c[1]-b[1]*c[0] };
		const double ca[3] = { c[1]*a[2]-c[2]*a[1], c[2]*a[0]-c[0]*a[2], c[0]*a[1]-c[1]*a[0] };
		const double ab[3] = { a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0] };
		const double det = a[0]*bc[0] + a[1]*bc[1] + a[2]*bc[2];
		const double norm = std::sqrt((a[0]*a[0]+a[1]*a[1]+a[2]*a[2]) *
			(b[0]*b[0]+b[1]*b[1]+b[2]*b[2]) * (c[0]*c[0]+c[1]*c[1]+c[2]*c[2]));
		if (!std::isfinite(norm) || norm < 1e-12 || std::fabs(det) < 1e-6 * norm) return false;
		for (int i = 0; i < 3; ++i) {
			const double value = (-m[12]*bc[i] - m[13]*ca[i] - m[15]*ab[i]) / det;
			if (!std::isfinite(value) || std::fabs(value) > 1e7) return false;
			out[i] = static_cast<float>(value);
		}
		return true;
	}

	// distance from the camera in metres, or < 0 when it cannot be read.
	//   1. _WorldSpaceCameraPos + object position  (euclidean)
	//   2. view matrix + object position           (euclidean)
	//   3. perspective view-projection centre      (euclidean)
	//   4. clip w fallback (axial depth only, never used for own tracking)
	inline float object_distance(program_info &p)
	{
		const float ox = p.world[12], oy = p.world[13], oz = p.world[14];
		const bool have_obj = p.obj_to_world.valid() && !(ox == 0.0f && oy == 0.0f && oz == 0.0f);

		float cam[3];
		bool from_uniform = false;
		if (have_obj && camera_of(p, cam, &from_uniform))
		{
			const float dx = ox - cam[0], dy = oy - cam[1], dz = oz - cam[2];
			p.dist_source = from_uniform ? "_WorldSpaceCameraPos" : "MATRIX_V";
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		}

		if (have_obj && p.view_proj.valid())
		{
			// clip.w = row3 . (pos, 1) = distance along the view axis
			const float w = p.vp_m[3] * ox + p.vp_m[7] * oy + p.vp_m[11] * oz + p.vp_m[15];
			if (std::isfinite(w) && w > 0.01f && w < 5000.0f)
			{
				if (perspective_camera(p.vp_m, cam))
				{
					const float dx = ox - cam[0], dy = oy - cam[1], dz = oz - cam[2];
					const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
					if (std::isfinite(distance)) { p.dist_source = "MATRIX_VP_CAMERA"; return distance; }
				}
				// Keep the old depth-based rendering fallback, but do not compare it
				// against Euclidean own-character distances from other programs.
				p.dist_source = "MATRIX_VP";
				return w;
			}
		}

		p.dist_source = "-";
		return -1.0f;
	}

	// Diagnostic only: project the object origin, not a mesh bounding box or
	// proof that a specific pixel belongs to this draw. Coordinates are in the
	// current OpenGL viewport with a bottom-left origin.
	inline bool diagnostic_project_origin(const program_info &p, const GLint vp[4], float out[2])
	{
		if (!p.obj_to_world.valid() || !p.view_proj.valid() || vp[2] <= 0 || vp[3] <= 0)
			return false;
		const float x = p.world[12], y = p.world[13], z = p.world[14];
		const float cx = p.vp_m[0]*x + p.vp_m[4]*y + p.vp_m[8]*z + p.vp_m[12];
		const float cy = p.vp_m[1]*x + p.vp_m[5]*y + p.vp_m[9]*z + p.vp_m[13];
		const float cw = p.vp_m[3]*x + p.vp_m[7]*y + p.vp_m[11]*z + p.vp_m[15];
		if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cw) || cw <= 0.0f)
			return false;
		out[0] = vp[0] + (cx / cw * 0.5f + 0.5f) * vp[2];
		out[1] = vp[1] + (cy / cw * 0.5f + 0.5f) * vp[3];
		return std::isfinite(out[0]) && std::isfinite(out[1]);
	}

	// ------------------------------------------------------------- GL state
	struct saved_state
	{
		GLboolean depth_test  = GL_FALSE;
		GLboolean blend       = GL_FALSE;
		GLboolean cull_face   = GL_FALSE;
		GLboolean depth_mask  = GL_TRUE;
		GLboolean color_mask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
		GLint     depth_func  = GL_LESS;
		GLint     blend_src_rgb = GL_ONE, blend_dst_rgb = GL_ZERO;
		GLint     blend_src_a = GL_ONE, blend_dst_a = GL_ZERO;
		GLfloat   line_width = 1.0f;
		GLint     blend_eq_rgb = GL_FUNC_ADD;   // the overlay uses GL_MAX for the line
		GLint     blend_eq_a   = GL_FUNC_ADD;
		GLint     polygon_mode[2] = { GL_FILL, GL_FILL };
		GLfloat   blend_color[4] = { 0, 0, 0, 0 };
		GLfloat   depth_range[2] = { 0.0f, 1.0f };
		GLboolean scissor_test  = GL_FALSE;   // the overlay renders full screen
		GLint     scissor_box[4] = { 0, 0, 16384, 16384 };
		GLboolean poly_offset   = GL_FALSE;   // FILL polygon offset (stroke copies)
		GLfloat   poly_factor   = 0.0f;
		GLfloat   poly_units    = 0.0f;
	};

	inline saved_state save_state()
	{
		saved_state st;
		GLboolean mask = GL_TRUE;

		st.depth_test = gl.IsEnabled(GL_DEPTH_TEST) ? GL_TRUE : GL_FALSE;
		st.blend      = gl.IsEnabled(GL_BLEND)      ? GL_TRUE : GL_FALSE;
		st.cull_face  = gl.IsEnabled(GL_CULL_FACE)  ? GL_TRUE : GL_FALSE;

		gl.GetBooleanv(GL_DEPTH_WRITEMASK, &mask);
		st.depth_mask = mask;
		gl.GetBooleanv(GL_COLOR_WRITEMASK, st.color_mask);
		gl.GetIntegerv(GL_DEPTH_FUNC, &st.depth_func);
		gl.GetIntegerv(GL_BLEND_SRC_RGB, &st.blend_src_rgb);
		gl.GetIntegerv(GL_BLEND_DST_RGB, &st.blend_dst_rgb);
		gl.GetIntegerv(GL_BLEND_SRC_ALPHA, &st.blend_src_a);
		gl.GetIntegerv(GL_BLEND_DST_ALPHA, &st.blend_dst_a);
		gl.GetFloatv(GL_LINE_WIDTH, &st.line_width);
		gl.GetIntegerv(GL_BLEND_EQUATION_RGB, &st.blend_eq_rgb);
		gl.GetIntegerv(GL_BLEND_EQUATION_ALPHA, &st.blend_eq_a);
		gl.GetIntegerv(GL_POLYGON_MODE, st.polygon_mode);
		gl.GetFloatv(GL_BLEND_COLOR, st.blend_color);
		gl.GetFloatv(GL_DEPTH_RANGE, st.depth_range);
		st.scissor_test = gl.IsEnabled(GL_SCISSOR_TEST) ? GL_TRUE : GL_FALSE;
		gl.GetIntegerv(GL_SCISSOR_BOX, st.scissor_box);
		st.poly_offset = gl.IsEnabled(GL_POLYGON_OFFSET_FILL) ? GL_TRUE : GL_FALSE;
		gl.GetFloatv(GL_POLYGON_OFFSET_FACTOR, &st.poly_factor);
		gl.GetFloatv(GL_POLYGON_OFFSET_UNITS,  &st.poly_units);
		return st;
	}

	inline void restore_state(const saved_state &st)
	{
		if (st.depth_test) gl.Enable(GL_DEPTH_TEST); else gl.Disable(GL_DEPTH_TEST);
		if (st.blend)      gl.Enable(GL_BLEND);      else gl.Disable(GL_BLEND);
		if (st.cull_face)  gl.Enable(GL_CULL_FACE);  else gl.Disable(GL_CULL_FACE);

		gl.ColorMask(st.color_mask[0], st.color_mask[1], st.color_mask[2], st.color_mask[3]);
		gl.DepthMask(st.depth_mask);
		gl.DepthFunc(static_cast<GLenum>(st.depth_func));
		gl.DepthRange(st.depth_range[0], st.depth_range[1]);
		gl.BlendFuncSeparate(static_cast<GLenum>(st.blend_src_rgb), static_cast<GLenum>(st.blend_dst_rgb),
		                     static_cast<GLenum>(st.blend_src_a), static_cast<GLenum>(st.blend_dst_a));
		gl.LineWidth(st.line_width);
		gl.BlendColor(st.blend_color[0], st.blend_color[1], st.blend_color[2], st.blend_color[3]);
		gl.BlendEquationSeparate(static_cast<GLenum>(st.blend_eq_rgb), static_cast<GLenum>(st.blend_eq_a));
		gl.PolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(st.polygon_mode[0]));
		gl.PolygonOffset(st.poly_factor, st.poly_units);
		if (st.poly_offset) gl.Enable(GL_POLYGON_OFFSET_FILL);
		else                gl.Disable(GL_POLYGON_OFFSET_FILL);
		gl.Scissor(st.scissor_box[0], st.scissor_box[1], st.scissor_box[2], st.scissor_box[3]);
		if (st.scissor_test) gl.Enable(GL_SCISSOR_TEST);
		else                 gl.Disable(GL_SCISSOR_TEST);
	}

	// additive tint, exactly like the original DLL:  result = src + dst * colour
	inline void begin_tint(const float colour[3], float strength)
	{
		gl.Enable(GL_BLEND);
		gl.BlendEquation(GL_FUNC_ADD);
		gl.BlendFunc(GL_ONE, GL_CONSTANT_COLOR);
		gl.BlendColor(colour[0] * strength, colour[1] * strength, colour[2] * strength, 1.0f);
		gl.Enable(GL_DEPTH_TEST);
		gl.DepthMask(GL_FALSE);
		gl.Disable(GL_CULL_FACE);
		gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}

	// ---- FILL (glow): whole body, depth comparison picks the part ---------
	template <typename F>
	inline void fill_pass(GLenum depth_func, const float colour[3], float strength, F &&real_draw)
	{
		begin_tint(colour, strength);
		gl.DepthFunc(depth_func);
		real_draw();
	}

	// ---- RING (stroke): the one and only 2D outline ------------------------
	// The mesh is copied 8 times, each copy shifted on screen by N pixels
	// (right / left / up / down + 4 diagonals). Union of the copies minus the
	// silhouette = a band of even width on the OUTSIDE of the character.
	//
	// Depth test is always GL_LESS, never GL_GREATER:
	//   inside the silhouette the body already wrote its depth, so a copy at the
	//   same place is not nearer and is rejected -> the body is never painted;
	//   outside the silhouette nothing of the character is in the depth buffer,
	//   so the copy wins and paints the band.
	// A polygon offset pushes the copies a hair away from the camera, which
	// keeps the interior clean on curved surfaces (arm / leg edges) where the
	// shifted copy can otherwise end up a few depth units in front of the body.
	//
	// GL_GREATER must NOT be used here. It was the "outline through walls"
	// pass of v2.6-v2.8 and on a curved, skinned body a shifted copy is behind
	// the body on about half of the interior pixels, so the whole character got
	// filled with the stroke colour (the 3D blobs in the screenshots).
	// Through-wall visibility is the depth claim's job now.
	// ---- BLEND FILL: the colour is mixed over the real texture -------------
	//   final = colour*strength + dst*(1 - colour*strength)
	// so the clothes, the skin and the face stay readable ("blend" mode).
	template <typename F>
	inline void blend_fill(GLenum depth_func, const float colour[3], float strength, F &&real_draw)
	{
		gl.Enable(GL_BLEND);
		gl.BlendEquation(GL_FUNC_ADD);
		gl.BlendFunc(GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR);
		gl.BlendColor(colour[0] * strength, colour[1] * strength, colour[2] * strength, 1.0f);
		gl.Enable(GL_DEPTH_TEST);
		gl.DepthFunc(depth_func);
		gl.DepthMask(GL_FALSE);
		gl.Disable(GL_CULL_FACE);
		gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		real_draw();
	}

	// ---- TEXTURED RE-DRAW: the character with the GAME's own shading -------
	// Used by the "real cloth" modes. The game's own draw happened normally
	// (that is the part in plain sight); this draws the same mesh a second time
	// with the game's own blending / textures, only where it is hidden behind a
	// wall, a car or another body (GL_GREATER). Nothing is recoloured, so what
	// you see through the wall is the enemy's actual clothes, hair and weapon.
	// Depth writes stay off, so neither the mask nor later draws are disturbed.
	template <typename F>
	inline void textured_pass(GLenum depth_func, const saved_state &st, F &&real_draw)
	{
		restore_state(st);              // the game's own blend / cull state
		gl.Enable(GL_DEPTH_TEST);
		gl.DepthFunc(depth_func);
		gl.DepthMask(GL_FALSE);
		gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		real_draw();
	}

	// ---- MASK: write one depth value over the silhouette, colour untouched --
	// This is the v2.3 trick that keeps the outline off the body:
	//   value 0.0 -> the character's pixels are nearer than everything, so a
	//                shifted copy can never pass GL_LESS there
	//   value 1.0 -> the character's pixels are farther than everything, so a
	//                shifted copy can never pass GL_GREATER there
	// It works even for meshes the game draws with depth writes DISABLED
	// (alpha blended cloth / hair), because this pass forces depth writes on.
	// The value must be cleaned up afterwards - see restore_depth below.
	template <typename F>
	inline void mask_depth(GLfloat value, F &&real_draw)
	{
		gl.Enable(GL_DEPTH_TEST);
		gl.DepthFunc(GL_ALWAYS);
		gl.DepthMask(GL_TRUE);
		gl.DepthRange(value, value);
		gl.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		gl.Disable(GL_BLEND);
		gl.Disable(GL_CULL_FACE);
		real_draw();
		gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		gl.DepthRange(0.0f, 1.0f);
	}

	// ---- RING (stroke): the silhouette is copied 8 times, each copy shifted on
	//      screen (right / left / up / down + 4 diagonals). Union of the copies
	//      minus the silhouette = a band of even width on the OUTSIDE only.
	//      depth_func GL_LESS   -> the part in plain sight
	//      depth_func GL_GREATER-> the part behind a wall / car / body
	//      Both need the matching mask_depth() call in front of them.
	template <typename F>
	inline void stroke_ring(GLenum depth_func, const float colour[3], float strength,
	                        GLint band_px, F &&real_draw)
	{
		GLint vp[4] = { 0, 0, 0, 0 };
		if (gl.GetIntegerv == nullptr || gl.Viewport == nullptr)
			return;
		gl.GetIntegerv(GL_VIEWPORT, vp);
		if (vp[2] <= 0 || vp[3] <= 0)
			return;

		const GLint r  = std::max(1, band_px);
		const GLint rd = std::max(1, static_cast<GLint>(std::lround(band_px * 0.7071f)));

		begin_tint(colour, strength);
		gl.DepthFunc(depth_func);

		const GLint dx[8] = {  r,  -r,   0,   0,  rd,  rd, -rd, -rd };
		const GLint dy[8] = {  0,   0,   r,  -r,  rd, -rd,  rd, -rd };

		for (int i = 0; i < 8; ++i)
		{
			gl.Viewport(vp[0] + dx[i], vp[1] + dy[i], vp[2], vp[3]);
			real_draw();
		}

		gl.Viewport(vp[0], vp[1], vp[2], vp[3]);
	}

	// ---- DEPTH RESTORE: put the REAL depth back over the mask --------------
	// The 0.0 / 1.0 mask must not survive the draw: the gun, the cloth, the hair
	// and the game's own rim / outline passes are drawn AFTER the body, and with
	// a mask left in the buffer they lose the depth test and leave a dark ring
	// around the character (that was the v2.3-v2.9 "dark / mask stroke").
	// Drawing the mesh once more with depth writes ON, GL_ALWAYS and the normal
	// depth range writes the true depth back.
	// Only done when the mesh really writes depth itself; a blended garment must
	// not start writing depth, so for those the mask stays at 1.0 (farther than
	// everything, which is harmless: every later draw passes it).
	template <typename F>
	inline void restore_depth(F &&real_draw)
	{
		gl.Enable(GL_DEPTH_TEST);
		gl.DepthFunc(GL_ALWAYS);
		gl.DepthMask(GL_TRUE);
		gl.DepthRange(0.0f, 1.0f);
		gl.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		gl.Disable(GL_BLEND);
		gl.Disable(GL_CULL_FACE);
		real_draw();
		gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}

	// ---- DEPTH CLAIM ("through walls") --------------------------------------
	// Writes depth (never colour) for the character and for the outline band,
	// with the depth range compressed into [0, high]. Everything drawn AFTER
	// this - the translucent ice / glue wall above all - is tested against a
	// value that is much nearer than the enemy really is, loses, and does not
	// paint over the chams.
	//
	//   ice wall on screen -> high 0.72, GL_ALWAYS : the wall cannot hide the chams
	//   normal             -> high 0.97, GL_GREATER: only where something is
	//                                               already in front of the enemy
	//                                               (a car, a wall, a body), so
	//                                               cloth, gun and the game's own
	//                                               rim passes keep their depth
	//
	// The depth test must stay ENABLED: with GL_DEPTH_TEST disabled OpenGL
	// writes no depth at all (that was the v1.7 bug).
	template <typename F>
	inline void claim_depth(GLint band_px, GLfloat high, GLenum depth_func, F &&real_draw)
	{
		GLint vp[4] = { 0, 0, 0, 0 };
		if (gl.GetIntegerv == nullptr || gl.Viewport == nullptr)
			return;
		gl.GetIntegerv(GL_VIEWPORT, vp);
		if (vp[2] <= 0 || vp[3] <= 0)
			return;

		high = std::clamp(high, 0.0f, 1.0f);

		gl.Enable(GL_DEPTH_TEST);
		gl.DepthFunc(depth_func);
		gl.DepthMask(GL_TRUE);
		gl.DepthRange(0.0f, high);
		gl.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		gl.Disable(GL_BLEND);
		gl.Disable(GL_CULL_FACE);

		// the character itself
		gl.Viewport(vp[0], vp[1], vp[2], vp[3]);
		real_draw();

		// and the outline band around it (so the wall cannot paint over the line).
		// 4 copies are enough here (up / down / left / right): this pass only
		// claims depth, it is not visible, so the diagonals would just cost time.
		if (band_px > 0)
		{
			const GLint r = std::max(1, band_px);
			const GLint dx[4] = {  r,  -r,   0,   0 };
			const GLint dy[4] = {  0,   0,   r,  -r };

			for (int i = 0; i < 4; ++i)
			{
				gl.Viewport(vp[0] + dx[i], vp[1] + dy[i], vp[2], vp[3]);
				real_draw();
			}

			gl.Viewport(vp[0], vp[1], vp[2], vp[3]);
		}

		gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		gl.DepthRange(0.0f, 1.0f);
	}

	// ---- WIRE (classic 3D stroke): the same triangles drawn as lines ---------
	// Own switch, own size, own colours. Independent of the 2D outline above.
	template <typename F>
	inline void wire_pass(GLenum depth_func, const float colour[3], float strength,
	                      GLfloat width, F &&real_draw)
	{
		begin_tint(colour, strength);
		gl.DepthFunc(depth_func);
		gl.PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		gl.LineWidth(width);
		real_draw();
		gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}

	// ---- DUMP MODE ----------------------------------------------------------
	// Writes one line per character-ish draw to  %TEMP%\chams-dump.txt
	// (NOT into the game folder: that one is usually not writable).
	// Purpose: find the real discriminator of "my own character" - program id,
	// index count, distance, world position and the shader's uniform names.
	inline int &dump_lines_left() { static int v = 2000; return v; }
	inline bool &dump_was_on()    { static bool v = false; return v; }

	inline void dump_draw(GLint program, GLsizei count, const program_info &p,
	                      float dist, bool has_uniform, bool wall_hint, float cam[3])
	{
		if (!g_settings.dump)
		{
			if (dump_was_on())
			{
				dump_was_on() = false;
				dump_lines_left() = 2000;
				dump_line("[CHAMS-DUMP] ---- stop ----");
			}
			return;
		}
		if (!dump_was_on())
		{
			dump_was_on() = true;
			dump_line("[CHAMS-DUMP] ---- start: every draw that looks like a character ----");
		}
		if (dump_lines_left() <= 0)
			return;
		--dump_lines_left();

		// uniform names of the shader program (up to 12, comma separated)
		char uniforms[512] = {};
		if (gl.GetProgramiv != nullptr && gl.GetActiveUniform != nullptr)
		{
			GLint n = 0;
			gl.GetProgramiv(static_cast<GLuint>(program), GL_ACTIVE_UNIFORMS, &n);
			if (n > 12) n = 12;
			for (GLint i = 0; i < n; ++i)
			{
				char name[64] = {};
				GLsizei len = 0; GLint size = 0; GLenum type = 0;
				gl.GetActiveUniform(static_cast<GLuint>(program), static_cast<GLuint>(i),
				                    sizeof(name), &len, &size, &type, name);
				if (name[0] == '\0') continue;

				// bounded append (strncat_s is MSVC only)
				const size_t used = std::strlen(uniforms);
				if (used + 2 >= sizeof(uniforms)) break;
				std::snprintf(uniforms + used, sizeof(uniforms) - used,
				              "%s%s", used ? "," : "", name);
			}
		}

		GLint vp[4] = {}, array_buffer = 0, element_buffer = 0, depth_func = 0;
		GLboolean depth_write = GL_FALSE;
		gl.GetIntegerv(GL_VIEWPORT, vp);
		gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
		gl.GetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &element_buffer);
		gl.GetIntegerv(GL_DEPTH_FUNC, &depth_func);
		gl.GetBooleanv(GL_DEPTH_WRITEMASK, &depth_write);
		float screen[2] = {};
		const bool screen_valid = diagnostic_project_origin(p, vp, screen);
		int wall_bits = 0;
		if (gl.GetUniformLocation != nullptr)
		{
			if (gl.GetUniformLocation(static_cast<GLuint>(program), "_FakeDepth_Ground") != -1) wall_bits |= 1;
			if (gl.GetUniformLocation(static_cast<GLuint>(program), "_IceColorBackground") != -1) wall_bits |= 2;
			if (gl.GetUniformLocation(static_cast<GLuint>(program), "_DepthTexMask") != -1) wall_bits |= 4;
		}
		char line[1800];
		std::snprintf(line, sizeof(line),
			"[CHAMS-DUMP] f=%u ctx=%p prog=%d count=%d dist=%.2f own=%d src=%s "
			"world=(%.2f,%.2f,%.2f) cam=(%.2f,%.2f,%.2f) chara_uniform=%d "
			"ownest=%.2f stable=%d hideown=%d hidden=%d detect=%d render=%d budget=%d match=%s "
			"owncap=%.2f wall_hint=%d wall_bits=%d viewport=(%d,%d,%d,%d) origin_valid=%d origin_gl=(%.1f,%.1f) "
			"array_buffer=%d element_buffer=%d depth_test=%d depth_func=0x%04X depth_write=%d uniforms=[%s]",
			g_frame_id.load(std::memory_order_relaxed), static_cast<void *>(g_opengl_context), program, count, dist,
			(dist >= 0.0f && dist < g_settings.own_distance) ? 1 : 0, p.dist_source,
			p.world[12], p.world[13], p.world[14], cam[0], cam[1], cam[2],
			has_uniform ? 1 : 0, own().own_est, own().stable, g_settings.hide_own ? 1 : 0,
			own_distance_excludes(g_settings, p, dist, own()) ? 1 : 0,
			g_settings.detect_mode, g_settings.render_mode, g_budget_left, character_token(program),
			g_settings.own_distance, wall_hint ? 1 : 0, wall_bits, vp[0], vp[1], vp[2], vp[3],
			screen_valid ? 1 : 0, screen[0], screen[1], array_buffer, element_buffer,
			gl.IsEnabled(GL_DEPTH_TEST) ? 1 : 0, static_cast<unsigned>(depth_func), depth_write ? 1 : 0, uniforms);

		dump_line(line);
	}

	// ---- DUMP ALL -----------------------------------------------------------
	// Logs EVERY draw call, including the ones the filter would skip, so a
	// missing piece (hair, gloves, a skin) can be spotted immediately.
	// Columns: mode, count, program, whether the shader declares the character
	// uniform, and the shader uniforms.
	inline int &dumpall_lines_left() { static int v = 3000; return v; }
	inline bool &dumpall_was_on()    { static bool v = false; return v; }

	inline void dump_all_draw(GLenum mode, GLsizei count, GLint program, bool has_uniform,
	                          bool depth_writes)
	{
		if (!g_settings.dump_all)
		{
			if (dumpall_was_on())
			{
				dumpall_was_on() = false;
				dumpall_lines_left() = 3000;
				dump_line("[CHAMS-DUMPALL] ---- stop ----");
			}
			return;
		}
		if (!dumpall_was_on())
		{
			dumpall_was_on() = true;
			dump_line("[CHAMS-DUMPALL] ---- start: every draw call ----");
		}
		if (dumpall_lines_left() <= 0)
			return;
		--dumpall_lines_left();

		char uniforms[512] = {};
		if (gl.GetProgramiv != nullptr && gl.GetActiveUniform != nullptr && program != 0)
		{
			GLint n = 0;
			gl.GetProgramiv(static_cast<GLuint>(program), GL_ACTIVE_UNIFORMS, &n);
			if (n > 14) n = 14;
			for (GLint i = 0; i < n; ++i)
			{
				char name[64] = {};
				GLsizei len = 0; GLint size = 0; GLenum type = 0;
				gl.GetActiveUniform(static_cast<GLuint>(program), static_cast<GLuint>(i),
				                    sizeof(name), &len, &size, &type, name);
				if (name[0] == '\0') continue;
				const size_t used = std::strlen(uniforms);
				if (used + 2 >= sizeof(uniforms)) break;
				std::snprintf(uniforms + used, sizeof(uniforms) - used,
				              "%s%s", used ? "," : "", name);
			}
		}

		char line[1024];
		std::snprintf(line, sizeof(line),
			"[CHAMS-DUMPALL] f=%u mode=0x%04X count=%d prog=%d chara_uniform=%d dw=%d uniforms=[%s]",
			g_frame_id.load(std::memory_order_relaxed), static_cast<unsigned>(mode), count, program,
			has_uniform ? 1 : 0, depth_writes ? 1 : 0, uniforms);
		dump_line(line);
	}

	// ---- ICE / GLUE WALL DETECTOR -------------------------------------------
	// Taken from the real game files (asset pack 1.132.1):
	//   shader  BRMobile/Special/Ice_FakeDepth_Ground
	//           uniforms _DepthTex, _DepthTexMask, _FakeDepth_Ground, _FadeDistancePow2,
	//                    _IceColorBackground, _DepthHeight, _Color, _Cull, _DestAlpha
	//   shader  BRMobile/Special/IcewallHighlightView
	//           uniforms _AvatarColor, _MainTex, mtx4x4unity_ObjectToWorld
	// Those uniforms are unique to the ice wall (the "throwable glue wall"), so a
	// draw that uses them means a wall exists on screen right now. While that is
	// true we push the chams harder in the depth buffer (the wall is translucent
	// and is drawn AFTER the characters, so depth is the only way to win).
	struct wall_cache { GLint program; bool is_wall; };
	struct wall_state
	{
		wall_cache e[16];
		int   count = 0;
		unsigned last_frame_seen = 0xFFFFFFFFu;
	};

	inline wall_state &walls() { static wall_state w; return w; }

	inline bool program_is_wall(GLint program)
	{
		if (program == 0 || gl.GetUniformLocation == nullptr)
			return false;

		wall_state &w = walls();
		for (int i = 0; i < w.count; ++i)
			if (w.e[i].program == program)
			{
				if (w.e[i].is_wall)
					w.last_frame_seen = g_frame_id;
				return w.e[i].is_wall;
			}

		// v2.6 also accepted "_AvatarColor" alone, which some HUD / minimap
		// shaders use, so the wall mode could switch on with no wall on screen -
		// and that mode stamps the whole silhouette (characters look flat).
		// v2.7 needed the real ice-wall uniforms; v3.1 is broader on purpose:
		// the throwable glue wall and the ice wall do not share one exact
		// uniform set, so every uniform of the program is scanned once for the
		// wall families we know from the game files (asset pack 1.132.1):
		//     BRMobile/Special/Ice_FakeDepth_Ground  (_FakeDepth_Ground,
		//        _IceColorBackground, _DepthTexMask, _DepthHeight, _FadeDistancePow2)
		//     BRMobile/WLGP/Ice*, BRMobile/MatCap/Ice
		//     the glue / throwable wall builds on the same fake-depth family
		// A character shader is never classified as a wall.
		const bool is_character_shader =
			gl.GetUniformLocation(static_cast<GLuint>(program), k_character_uniform) != -1;

		bool is_wall = false;
		if (!is_character_shader && gl.GetProgramiv != nullptr && gl.GetActiveUniform != nullptr)
		{
			GLint n = 0;
			gl.GetProgramiv(static_cast<GLuint>(program), GL_ACTIVE_UNIFORMS, &n);
			if (n > 48) n = 48;

			for (GLint i = 0; i < n && !is_wall; ++i)
			{
				char name[64] = {};
				GLsizei len = 0; GLint size = 0; GLenum type = 0;
				gl.GetActiveUniform(static_cast<GLuint>(program), static_cast<GLuint>(i),
				                    sizeof(name), &len, &size, &type, name);
				if (name[0] == '\0')
					continue;

				// lower-case compare, only the wall-specific words
				char low[64] = {};
				for (int c = 0; c < 63 && name[c]; ++c)
					low[c] = (name[c] >= 'A' && name[c] <= 'Z') ? static_cast<char>(name[c] + 32) : name[c];

				if (std::strstr(low, "fakedepth") || std::strstr(low, "depthtexmask") ||
					std::strstr(low, "depthheight") || std::strstr(low, "icecolor") ||
					std::strstr(low, "glue") ||
					(std::strstr(low, "ice") && (std::strstr(low, "depth") || std::strstr(low, "wall"))))
					is_wall = true;
			}
		}

		// fall back to the exact names (cheap, and works even when the active
		// uniform list is not available)
		if (!is_wall && !is_character_shader && gl.GetUniformLocation != nullptr)
		{
			const bool has_fake_depth =
				gl.GetUniformLocation(static_cast<GLuint>(program), "_FakeDepth_Ground") != -1;
			const bool has_ice_bg =
				gl.GetUniformLocation(static_cast<GLuint>(program), "_IceColorBackground") != -1;
			const bool has_depth_mask =
				gl.GetUniformLocation(static_cast<GLuint>(program), "_DepthTexMask") != -1;
			is_wall = has_fake_depth || (has_ice_bg && has_depth_mask);
		}

		if (w.count < 16)
		{
			w.e[w.count].program = program;
			w.e[w.count].is_wall = is_wall;
			++w.count;
		}

		if (is_wall)
		{
			w.last_frame_seen = g_frame_id;
			reshade::log::message(reshade::log::level::info,
				"[CHAMS] ice / glue wall detected (program %d) - strong depth claim active", program);
		}
		return is_wall;
	}

	// a wall was drawn very recently -> protect the chams
	inline bool wall_on_screen()
	{
		const unsigned seen = walls().last_frame_seen;
		return seen != 0xFFFFFFFFu && g_frame_id - seen < 5;
	}

	// the overlay renderer (mask framebuffer + composite) - included here so
	// draw_chams() below can capture into it; it needs every helper above
#include "chams_overlay.inl"

	// Shared by all draw-wrapper template instantiations on this thread.
	inline frame_trace &draw_trace() { static thread_local frame_trace trace; return trace; }

	// Overlay bookkeeping is shared by every draw wrapper, like ov() itself.
	// Keeping these statics inside draw_chams<F> would create one copy per
	// entry-point lambda, repeatedly resetting the shared mask on mode changes.
	inline int &previous_render_mode() { static int mode = -1; return mode; }
	inline void observe_overlay_menu(bool is_open)
	{
		static bool was_open = false;
		if (was_open && !is_open)
		{
			overlay_gpu &g = ov();
			g.captured_frame = 0xFFFFFFFFu;
			g.has_mask = false;
			reshade::log::message(reshade::log::level::info,
				"[CHAMS] menu closed - overlay state reset, chams resumed");
		}
		was_open = is_open;
	}

	struct capture_program_metadata
	{
		uintptr_t context = 0; uint64_t epoch = 0; GLint program = 0; int tokens = 0, skin = 0, wall = 0;
		matrix_loc object, view, projection; GLint camera = -1;
	};
	inline std::string capture_json_string(const char *data, size_t len)
	{
		static const char hex[] = "0123456789abcdef";
		std::string s = "\"";
		for (size_t i = 0; i < len; ++i) {
			const unsigned char c = static_cast<unsigned char>(data[i]);
			if (c == '\\' || c == '\"') { s += '\\'; s += static_cast<char>(c); }
			else if (c < 32 || c >= 127) { s += "\\u00"; s += hex[c >> 4]; s += hex[c & 15]; }
			else s += static_cast<char>(c);
		}
		return s + "\"";
	}
	inline const capture_program_metadata &capture_program(uintptr_t context, GLint program)
	{
		struct cache_type { uint64_t epoch = 0; unsigned used = 0; capture_program_metadata entries[128]; };
		static thread_local cache_type cache;
		const uint64_t epoch = capture::metadata_generation();
		if (cache.epoch != epoch) { cache.epoch = epoch; cache.used = 0; }
		for (unsigned i = 0; i < std::min(cache.used, 128u); ++i)
			if (cache.entries[i].context == context && cache.entries[i].program == program) return cache.entries[i];
		auto &p = cache.entries[cache.used++ % 128]; p = capture_program_metadata{};
		p.context = context; p.program = program; p.epoch = epoch;
		for (size_t i = 0; i < sizeof(k_chara_tokens)/sizeof(k_chara_tokens[0]); ++i)
			if (gl.GetUniformLocation(program, k_chara_tokens[i]) != -1) p.tokens |= (1 << i);
		for (size_t i = 0; i < sizeof(k_skin_uniforms)/sizeof(k_skin_uniforms[0]); ++i)
			if (gl.GetUniformLocation(program, k_skin_uniforms[i]) != -1) p.skin |= (1 << i);
		if (gl.GetUniformLocation(program, "_FakeDepth_Ground") != -1) p.wall |= 1;
		if (gl.GetUniformLocation(program, "_IceColorBackground") != -1) p.wall |= 2;
		if (gl.GetUniformLocation(program, "_DepthTexMask") != -1) p.wall |= 4;
		static const char *const objects[] = {"hlslcc_mtx4x4unity_ObjectToWorld", "unity_ObjectToWorld", "_ObjectToWorld", "unity_ObjectToWorldArray"};
		static const char *const views[] = {"hlslcc_mtx4x4unity_MatrixV", "UNITY_MATRIX_V", "unity_MatrixV"};
		static const char *const projections[] = {"hlslcc_mtx4x4unity_MatrixVP", "UNITY_MATRIX_VP", "unity_MatrixVP"};
		p.object = resolve_matrix(program, objects, 4); p.view = resolve_matrix(program, views, 3);
		p.projection = resolve_matrix(program, projections, 3); p.camera = gl.GetUniformLocation(program, "_WorldSpaceCameraPos");
		// Emit introspection once per cached context/program/epoch. Bounded driver
		// names and counts are explicitly flagged if truncated; no shader source.
		try {
			GLint uniform_count = 0, shader_count = 0, link_status = 0;
			if (gl.GetProgramiv) { gl.GetProgramiv(program, GL_ACTIVE_UNIFORMS, &uniform_count);
				gl.GetProgramiv(program, GL_ATTACHED_SHADERS, &shader_count); gl.GetProgramiv(program, GL_LINK_STATUS, &link_status); }
			const int safe_uniforms = std::max(0, std::min(uniform_count, 4096));
			const int safe_shaders = std::max(0, std::min(shader_count, 32));
			std::string head = "\"program\":" + std::to_string(program) + ",\"epoch\":" + std::to_string(epoch) +
				",\"link_status\":" + std::to_string(link_status) + ",\"program_query_available\":" + std::to_string(gl.GetProgramiv ? 1 : 0) +
				",\"uniform_count\":" + std::to_string(uniform_count) + ",\"uniform_query_available\":" + std::to_string(gl.GetActiveUniform ? 1 : 0) +
				",\"uniforms_recordable\":" + std::to_string(gl.GetActiveUniform ? safe_uniforms : 0) +
				",\"shader_count\":" + std::to_string(shader_count) + ",\"shader_query_available\":" + std::to_string(gl.GetAttachedShaders && gl.GetShaderiv ? 1 : 0) +
				",\"shaders_recordable\":" + std::to_string(gl.GetAttachedShaders && gl.GetShaderiv ? safe_shaders : 0);
			capture::runtime_inflight_event(context, "program_info", head);
			if (gl.GetActiveUniform) for (int i = 0; i < safe_uniforms; ++i) {
				char name[256] = {}; GLsizei len = 0; GLint size = 0; GLenum type = 0;
				gl.GetActiveUniform(program, i, sizeof(name), &len, &size, &type, name);
				const size_t chars = static_cast<size_t>(std::max(0, std::min<int>(len, sizeof(name)-1)));
				std::string row = "\"program\":" + std::to_string(program) + ",\"epoch\":" + std::to_string(epoch) +
					",\"index\":" + std::to_string(i) + ",\"name\":" + capture_json_string(name, chars) +
					",\"name_truncated\":" + (len >= static_cast<GLsizei>(sizeof(name)-1) ? "true" : "false") +
					",\"size\":" + std::to_string(size) + ",\"type\":" + std::to_string(type);
				capture::runtime_inflight_event(context, "program_uniform", row);
			}
			if (gl.GetAttachedShaders && gl.GetShaderiv && safe_shaders) {
				GLuint shaders[32] = {}; GLsizei got = 0; gl.GetAttachedShaders(program, safe_shaders, &got, shaders);
				for (GLsizei i = 0; i < got && i < safe_shaders; ++i) { GLint type = 0; gl.GetShaderiv(shaders[i], GL_SHADER_TYPE, &type);
					std::string row = "\"program\":" + std::to_string(program) + ",\"epoch\":" + std::to_string(epoch) +
						",\"shader\":" + std::to_string(shaders[i]) + ",\"type\":" + std::to_string(type);
					capture::runtime_inflight_event(context, "program_shader", row);
				}
			}
		} catch (...) { capture::runtime_record_loss(); }
		return p;
	}

	// GL may return a full mat4 even for a location spelled like an array.
	// Always query into a 16-float scratch buffer so a surprising uniform type
	// cannot overwrite the capture object's adjacent fields.
	inline void capture_read_matrix(GLint program, const matrix_loc &loc, GLfloat out[16])
	{
		if (!loc.valid() || !gl.GetUniformfv) return;
		if (loc.array_style) for (int i = 0; i < 4; ++i) {
			GLfloat tmp[16] = {}; gl.GetUniformfv(program, loc.loc[i], tmp);
			std::memcpy(out + 4*i, tmp, 4*sizeof(GLfloat));
		} else { GLfloat tmp[16] = {}; gl.GetUniformfv(program, loc.plain, tmp); std::memcpy(out, tmp, sizeof(tmp)); }
	}

	// Snapshot original game draw state; the guard records even early exits.
	// Numeric IDs are GL names, not game object identities. No game memory,
	// shader source, texture contents or host file paths are recorded here.
	struct capture_gl_scope
	{
		bool active = false; uintptr_t context = 0; uint64_t surface = 0, program_epoch = 0, preparation_ns = 0;
		capture::draw_ticket ticket;
		const char *api = "unknown", *decision = "not_evaluated";
		GLint mode = 0, count = 0, program = 0, array = 0, element = 0, indirect_buffer = 0, vao = 0, framebuffer = 0;
		GLint first = -1, instances = -1, index_type = -1, base_vertex = 0, draw_count = -1, stride = -1;
		uint64_t base_instance = 0, index_offset = 0, indirect_offset = 0, range_start = 0, range_end = 0;
		int has_index_offset = 0, has_indirect_offset = 0, has_range = 0;
		GLint viewport[4] = {}, scissor_box[4] = {}, depth_func = 0, blend_src = 0, blend_dst = 0;
		GLint blend_src_alpha = 0, blend_dst_alpha = 0, blend_eq_rgb = 0, blend_eq_alpha = 0;
		GLint active_texture = 0, texture_2d = 0, cull_mode = 0, read_framebuffer = 0;
		GLfloat depth_range[2] = {}; GLboolean color_mask[4] = {};
		int depth_test = 0, depth_write = 0, blend = 0, scissor = 0, cull = 0, uniform_bits = 0, skin_bits = 0, wall_bits = 0, wall_hint = -1, matrix_bits = 0;
		int detect = 0, renderer = 0, hide = 0, budget = 0, stable = 0, has_info = 0;
		int enabled = 0, through = 0, glow = 0, stroke = 0, wire = 0; float own_cap = 0;
		float estimate = -1, distance = -1, world[16] = {}, view[16] = {}, vp[16] = {}, camera[3] = {};
		const char *dist_source = "unknown";
		capture_gl_scope(const char *name, GLenum prim, GLsizei n) : api(name), mode(prim), count(n)
		{
#if defined(_WIN32) && defined(CHAMS_HOST_CALLER_PROBE)
			caller_probe::note_gate_entry();
#endif
			if (!capture::runtime_enabled())
			{
#if defined(_WIN32) && defined(CHAMS_HOST_CALLER_PROBE)
				caller_probe::note_gate_disabled();
#endif
				return;
			}
			const auto start = std::chrono::steady_clock::now();
			if (!capture::runtime_begin_draw())
			{
#if defined(_WIN32) && defined(CHAMS_HOST_CALLER_PROBE)
				caller_probe::note_begin_draw_failed();
#endif
				return;
			}
			active = true; context = capture::native_context(); surface = capture::runtime_inflight_surface(capture::native_surface());
			ticket = capture::runtime_bind_draw(context, surface);
#if defined(_WIN32) && defined(CHAMS_HOST_CALLER_PROBE)
			caller_probe::sample(context, surface, ticket);
#endif
			detect = g_settings.detect_mode; renderer = g_settings.render_mode; hide = g_settings.hide_own; budget = g_budget_left;
			enabled = g_settings.enabled; through = g_settings.through_walls; glow = g_settings.glow; stroke = g_settings.stroke; wire = g_settings.wire;
			own_cap = std::isfinite(g_settings.own_distance) ? g_settings.own_distance : -1;
			if (!g_opengl_context || !gl.GetIntegerv) {
				preparation_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count()); return;
			}
			gl.GetIntegerv(GL_CURRENT_PROGRAM, &program);
			gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &array); gl.GetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &element);
			gl.GetIntegerv(GL_DRAW_INDIRECT_BUFFER_BINDING, &indirect_buffer);
			gl.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao); gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
			gl.GetIntegerv(GL_VIEWPORT, viewport); gl.GetIntegerv(GL_DEPTH_FUNC, &depth_func);
			gl.GetIntegerv(GL_BLEND_SRC_RGB, &blend_src); gl.GetIntegerv(GL_BLEND_DST_RGB, &blend_dst);
			gl.GetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha); gl.GetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_alpha);
			gl.GetIntegerv(GL_BLEND_EQUATION_RGB, &blend_eq_rgb); gl.GetIntegerv(GL_BLEND_EQUATION_ALPHA, &blend_eq_alpha);
			gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_framebuffer); gl.GetIntegerv(GL_CULL_FACE_MODE, &cull_mode);
			gl.GetIntegerv(GL_ACTIVE_TEXTURE, &active_texture); gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &texture_2d);
			gl.GetIntegerv(GL_SCISSOR_BOX, scissor_box);
			if (gl.IsEnabled) { depth_test = gl.IsEnabled(GL_DEPTH_TEST); blend = gl.IsEnabled(GL_BLEND); scissor = gl.IsEnabled(GL_SCISSOR_TEST); cull = gl.IsEnabled(GL_CULL_FACE); }
			if (gl.GetBooleanv) { GLboolean b = GL_FALSE; gl.GetBooleanv(GL_DEPTH_WRITEMASK, &b); depth_write = b; gl.GetBooleanv(GL_COLOR_WRITEMASK, color_mask); }
			if (gl.GetFloatv) gl.GetFloatv(GL_DEPTH_RANGE, depth_range);
			if (program && gl.GetUniformLocation) {
				const auto &m = capture_program(context, program);
				uniform_bits = m.tokens; skin_bits = m.skin; wall_bits = m.wall; program_epoch = m.epoch;
				program_info p{}; p.program = program; p.obj_to_world = m.object; p.view = m.view; p.view_proj = m.projection;
				matrix_bits = (m.object.valid() ? 1 : 0) | (m.view.valid() ? 2 : 0) | (m.projection.valid() ? 4 : 0) | (m.camera != -1 ? 8 : 0);
				if (gl.GetUniformfv && matrix_bits) {
					capture_read_matrix(program, m.object, p.world); capture_read_matrix(program, m.view, p.view_m); capture_read_matrix(program, m.projection, p.vp_m);
					if (m.camera != -1) { GLfloat tmp[16] = {}; gl.GetUniformfv(program, m.camera, tmp); std::memcpy(p.cam_uniform, tmp, sizeof(p.cam_uniform)); p.loc_cam_pos = m.camera; }
					state(p, object_distance(p), false);
				}
			}
			preparation_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
		}
		void index_pointer(const void *p) { if (active && element != 0) { has_index_offset = 1; index_offset = reinterpret_cast<uintptr_t>(p); } }
		void indirect_pointer(const void *p) { if (active && indirect_buffer != 0) { has_indirect_offset = 1; indirect_offset = reinterpret_cast<uintptr_t>(p); } }
		void state(const program_info &p, float d, bool capture_tracker = true)
		{
			if (!active) return;
			has_info = 1; distance = std::isfinite(d) ? d : -1; dist_source = p.dist_source;
			std::memcpy(world, p.world, sizeof(world)); std::memcpy(view, p.view_m, sizeof(view));
			std::memcpy(vp, p.vp_m, sizeof(vp)); std::memcpy(camera, p.cam_uniform, sizeof(camera));
			if (capture_tracker) { estimate = std::isfinite(own().own_est) ? own().own_est : -1; stable = own().stable; }
		}
		~capture_gl_scope()
		{
			if (!active) return;
			const auto serialize_start = std::chrono::steady_clock::now();
			try {
				char b[4096]; int n = std::snprintf(b, sizeof(b),
					"\"api\":\"%s\",\"mode\":%d,\"count\":%d,\"first\":%d,\"instances\":%d,\"index_type\":%d,\"base_vertex\":%d,\"base_instance\":%llu,\"draw_count\":%d,\"indirect_stride\":%d,\"program\":%d,\"program_epoch\":%llu,\"array_buffer\":%d,\"element_buffer\":%d,\"indirect_buffer\":%d,\"has_index_offset\":%d,\"index_offset\":%llu,\"has_indirect_offset\":%d,\"indirect_offset\":%llu,\"has_range\":%d,\"range_start\":%llu,\"range_end\":%llu,\"vao\":%d,\"framebuffer\":%d,\"viewport\":[%d,%d,%d,%d],\"depth_test\":%d,\"depth_write\":%d,\"depth_func\":%d,\"blend\":%d,\"blend_src\":%d,\"blend_dst\":%d,\"token_bits\":%d,\"skin_bits\":%d,\"wall_bits\":%d,\"wall_hint\":%d,\"matrix_bits\":%d,\"detect\":%d,\"renderer\":%d,\"hide_own\":%d,\"enabled\":%d,\"through_walls\":%d,\"glow\":%d,\"stroke\":%d,\"wire\":%d,\"budget\":%d,\"decision\":\"%s\",\"has_info\":%d,\"distance_source\":\"%s\",\"stable\":%d",
					api, mode, count, first, instances, index_type, base_vertex, static_cast<unsigned long long>(base_instance), draw_count, stride, program, static_cast<unsigned long long>(program_epoch), array, element, indirect_buffer,
					has_index_offset, static_cast<unsigned long long>(index_offset), has_indirect_offset, static_cast<unsigned long long>(indirect_offset), has_range, static_cast<unsigned long long>(range_start), static_cast<unsigned long long>(range_end), vao, framebuffer, viewport[0], viewport[1], viewport[2], viewport[3],
					depth_test, depth_write, depth_func, blend, blend_src, blend_dst, uniform_bits, skin_bits, wall_bits, wall_hint, matrix_bits, detect, renderer, hide, enabled, through, glow, stroke, wire, budget, decision, has_info, dist_source, stable);
				if (n < 0 || static_cast<size_t>(n) >= sizeof(b)) { capture::runtime_end_draw(ticket, nullptr); return; }
				std::string body(b, static_cast<size_t>(n));
				body += ",\"read_framebuffer\":" + std::to_string(read_framebuffer) + ",\"active_texture\":" + std::to_string(active_texture) + ",\"texture_2d\":" + std::to_string(texture_2d) +
					",\"blend_src_alpha\":" + std::to_string(blend_src_alpha) + ",\"blend_dst_alpha\":" + std::to_string(blend_dst_alpha) +
					",\"blend_eq_rgb\":" + std::to_string(blend_eq_rgb) + ",\"blend_eq_alpha\":" + std::to_string(blend_eq_alpha) +
					",\"scissor\":" + std::to_string(scissor) + ",\"cull\":" + std::to_string(cull) + ",\"cull_mode\":" + std::to_string(cull_mode) +
					",\"scissor_box\":[" + std::to_string(scissor_box[0]) + "," + std::to_string(scissor_box[1]) + "," + std::to_string(scissor_box[2]) + "," + std::to_string(scissor_box[3]) + "]" +
					",\"color_mask\":[" + std::to_string(color_mask[0]) + "," + std::to_string(color_mask[1]) + "," + std::to_string(color_mask[2]) + "," + std::to_string(color_mask[3]) + "]" +
					",\"depth_range\":[" + capture::json_number(depth_range[0]) + "," + capture::json_number(depth_range[1]) + "]";
				body += ",\"own_cap\":" + capture::json_number(own_cap) + ",\"distance\":" + capture::json_number(distance) + ",\"own_est\":" + capture::json_number(estimate);
				auto append = [&](const char *key, const float *v, int size) {
					body += ",\""; body += key; body += "\":[";
					for (int i = 0; i < size; ++i) { if (i) body += ","; if (!std::isfinite(v[i])) body += "null";
						else body += capture::json_number(v[i]); } body += "]";
				};
				if (has_info) { append("world", world, 16); append("view", view, 16); append("view_projection", vp, 16); append("camera_uniform", camera, 3); }
				capture::runtime_instrumentation_time(preparation_ns + static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - serialize_start).count()));
				capture::runtime_end_draw(ticket, &body);
			} catch (...) { capture::runtime_end_draw(ticket, nullptr); }
		}
	};

	// --------------------------------------------------------------- wrapper
	template <typename F>
	// index_offset: raw `indices` pointer value when an element buffer is bound
	// (GL treats it as a byte offset), 0 for non-indexed draws. Only feeds the
	// Stage 2 fingerprint; never used for the distance predicate.
	inline void draw_chams(GLenum mode, GLsizei count, F &&real_draw, capture_gl_scope *cap = nullptr, uint64_t index_offset = 0)
	{
		if (g_opengl_context != nullptr)
			ensure_loaded();
		settings &s = g_settings;

		// Re-arm bounded dumps when switched off, including while the menu is
		// open or the draw budget is exhausted. The old stop branches were
		// unreachable for Dump ALL because it was only called while enabled.
		if (g_opengl_context != nullptr && !s.dump_all && dumpall_was_on())
			dump_all_draw(0, 0, 0, false, false);
		if (g_opengl_context != nullptr && !s.dump && dump_was_on())
		{
			dump_was_on() = false;
			dump_lines_left() = 2000;
			dump_line("[CHAMS-DUMP] ---- stop ----");
		}

		// Count wrapper decisions only while Dump ALL is enabled. Counters are
		// per-thread and per-context segment; they do not imply object identity.
		frame_trace &trace = draw_trace();
		frame_trace *diag = trace.enter(s.dump_all, g_opengl_context, g_frame_id,
			s.detect_mode, s.render_mode, s.hide_own,
			[](const char *line) { dump_line(line); }) ? &trace : nullptr;

		// Observe the open state before the early return; otherwise the menu
		// close transition below can never be detected.
		if (g_opengl_context != nullptr)
			observe_overlay_menu(g_overlay_open);

		if (!s.enabled || g_opengl_context == nullptr || g_overlay_open ||
			g_budget_left <= 0 || !is_solid_primitive(mode) || count <= 0)
		{
			if (diag != nullptr)
			{
				// Reason counters overlap when multiple gates are closed.
				if (!s.enabled) ++diag->disabled;
				if (g_overlay_open) ++diag->menu;
				if (g_budget_left <= 0) ++diag->budget;
				if (!is_solid_primitive(mode) || count <= 0) ++diag->primitive;
			}
			if (cap) cap->decision = !s.enabled ? "disabled" : g_opengl_context == nullptr ? "no_context" : g_overlay_open ? "menu_open" : g_budget_left <= 0 ? "budget" : "primitive";
			real_draw();
			return;
		}

		touch_frame();
		if (diag != nullptr) { diag->own_est = own().own_est; diag->own_stable = own().stable; }

		// When switching between Direct and Overlay modes, reset the overlay
		// state so the mask is always cleared on the first overlay frame.
		// Stale captured_frame / has_mask from a previous overlay session
		// could cause the composite to skip (captured_frame mismatch) or
		// composite a stale mask, both of which can lead to a black screen.
		int &prev_render_mode = previous_render_mode();
		if (prev_render_mode != s.render_mode)
		{
			overlay_gpu &g = ov();
			g.captured_frame = 0xFFFFFFFFu;
			g.has_mask = false;
			// Reuse the existing GL objects when switching modes.
			prev_render_mode = s.render_mode;
			// Pre-build the overlay renderer when switching TO overlay mode, so
			// shader compilation happens during rendering (not during wglSwapBuffers
			// where it could stall the GPU pipeline and freeze the game). Called
			// once on mode switch only - not every frame - so there are no
			// repeated compilation stalls during gameplay.
			if (s.render_mode == 0)
				overlay_build();
		}

		GLint program = 0;
		if (gl.GetIntegerv) gl.GetIntegerv(GL_CURRENT_PROGRAM, &program);

		// Preserve the existing wall heuristic; its result is diagnostic only
		// and is not a validated object-identity or character-exclusion rule.
		const bool wall_hint = gl.GetIntegerv != nullptr && program_is_wall(program);
		if (cap) cap->wall_hint = wall_hint ? 1 : 0;

		// "dump all" runs before every filter, so skipped pieces show up too
		if (s.dump_all)
		{
			const bool has_uniform = program != 0 && gl.GetUniformLocation != nullptr &&
				gl.GetUniformLocation(static_cast<GLuint>(program), k_character_uniform) != -1;
			// dw = does this draw write depth (a ring can only be built on it)
			GLboolean dw = GL_TRUE;
			if (gl.GetBooleanv) gl.GetBooleanv(GL_DEPTH_WRITEMASK, &dw);

			dump_all_draw(mode, count, program, has_uniform, dw == GL_TRUE);
		}

		if (mode != GL_TRIANGLES || count < k_min_index_count)
		{
			if (diag != nullptr) ++diag->filtered;
			if (cap) cap->decision = "primitive_or_count";
			real_draw();
			return;
		}

		if (!program_is_character(program))
		{
			if (diag != nullptr) ++diag->filtered;
			if (cap) cap->decision = "classifier_reject";
			real_draw();
			return;
		}
		if (diag != nullptr) ++diag->matched;

		program_info *info = get_program_info(program);
		if (info == nullptr)
		{
			if (diag != nullptr) ++diag->no_info;
			if (cap) cap->decision = "no_program_info";
			real_draw();
			return;
		}
		read_matrices(*info);

		const float dist = object_distance(*info);
		if (cap) cap->state(*info, dist);

		float cam[3] = { 0.0f, 0.0f, 0.0f };
		camera_of(*info, cam);

		// dump mode: log what we see, then continue normally
		dump_draw(program, count, *info, dist,
		          gl.GetUniformLocation != nullptr &&
		          gl.GetUniformLocation(static_cast<GLuint>(program), k_character_uniform) != -1,
		          wall_hint, cam);

		// Distance-only ownership remains a heuristic. All measurements used
		// here must have the same Euclidean meaning; clip-w is axial depth.
		const bool comparable_distance = std::strcmp(info->dist_source, "MATRIX_VP") != 0;
		if (comparable_distance && dist > 0.0f)
		{
			note_character_distance(dist);
			if (dist < own().frame_min)
				own().frame_min = dist;
		}

		// Stage 2 (F1): a bound own-attachment fork leaves the estimate band
		// entirely; hiding is gated on its learned fingerprint plus this
		// frame's in-span position (never on co-rendering alone). Root-member
		// bookkeeping happens inside the call as well, so it precedes the
		// distance predicate without changing its outcome.
		if (s.hide_own && comparable_distance && dist > 0.0f &&
			own_fork_note_draw(s, *info, dist, own(), program, count, index_offset))
		{
			if (diag != nullptr) ++diag->own_hidden;
			if (cap) cap->decision = "hide_own";
			real_draw();
			return;
		}

		if (own_distance_excludes(s, *info, dist, own()))
		{
			if (diag != nullptr) ++diag->own_hidden;
			if (cap) cap->decision = "hide_own";
			real_draw();
			return;
		}

		// the "real clothes" modes draw the character even at strength 0
		// (they are not colour based), so only the solid / blend modes need a
		// non-zero strength
		const bool want_glow   = s.glow && (s.glow_alpha > 0.0f ||
		                        s.glow_mode == settings::glow_cloth_tint ||
		                        s.glow_mode == settings::glow_cloth_only);
		const bool want_stroke = s.stroke && s.stroke_alpha > 0.0f;
		const bool want_wire   = s.wire   && s.wire_alpha   > 0.0f;
		if (!want_glow && !want_stroke && !want_wire)
		{
			if (diag != nullptr) ++diag->no_effect;
			if (cap) cap->decision = "no_effect";
			real_draw();
			return;
		}

		--g_budget_left;

		// band width in pixels. The outline keeps the same weight relative to the
		// character on screen: near = big character = thicker band, far = thinner
		// band, but never under 2 px so a distant enemy still gets a clear line.
		// Two independent widths: open area (plain sight) and covered area
		// (behind a wall / glue wall / car / another body).
		float scale = 1.0f;
		if (dist > 0.0f)
			scale = std::clamp(6.0f / dist, 0.8f, 2.0f);
		const GLint band_px_open  = want_stroke
			? std::max(2, static_cast<int>(std::lround(static_cast<float>(s.stroke_open_size) * scale)))
			: 0;
		const GLint band_px_through = want_stroke
			? std::max(2, static_cast<int>(std::lround(static_cast<float>(s.stroke_through_size) * scale)))
			: 0;

		// THROUGH WALLS: one switch, no percentage (see claim_depth above).
		//   ON  -> GL_ALWAYS, depth range 0..0.0: the character is written as the
		//          nearest thing in the depth buffer, so ANYTHING drawn afterwards
		//          - the translucent ice wall, the throwable glue wall, a wall
		//          skin, a container - loses the depth test and cannot paint over
		//          the chams. This is deterministic: it does not depend on the
		//          draw order or on detecting the wall shader before the character.
		//   OFF -> GL_GREATER, depth range 0..0.97: nothing is claimed in the
		//          open, only where something is already in front of the enemy.
		// wall_on_screen() is kept only for the log line ("ice wall detected").
		const GLfloat claim_high = s.through_walls ? k_claim_near : k_claim_gentle;
		const GLenum  claim_func = s.through_walls ? GL_ALWAYS : GL_GREATER;
		(void)wall_on_screen();

		const saved_state st = save_state();

		// ---- 1. the game's own draw (normal state, body keeps its look) ----
		restore_state(st);
		real_draw();

		// ---- OVERLAY RENDERER (overlay mode) --------------------------------
		// The mesh is written into OUR OWN mask framebuffer (alpha = coverage).
		// At the end of the frame, render_overlay() composites the mask on top
		// of the finished image. Nothing in the scene can hide it - not glue
		// walls, not wall skins, not containers. If capture fails for any
		// reason, fall through to the Direct mode passes below so chams still
		// work (never zero chams).
		bool overlay_captured = false;
		if (s.render_mode == 0 && overlay_build())
		{
			overlay_capture_mesh(real_draw);
			// Capture only counts if the mask was actually written this frame.
			overlay_gpu &g2 = ov();
			overlay_captured = g2.has_mask && g2.captured_frame == g_frame_id;
			restore_state(st);
			if (overlay_captured)
			{
				if (diag != nullptr) ++diag->overlay;
				if (cap) cap->decision = "overlay";
				return;   // overlay path succeeded: Direct mode passes not needed
			}
			// overlay capture did not happen -> fall through to Direct mode below
		}
		if (diag != nullptr)
		{
			++diag->direct;
			if (s.render_mode == 0) ++diag->overlay_failed;
		}

		if (cap) cap->decision = s.render_mode == 0 ? "direct_overlay_fallback" : "direct";
		// ---- 2. GLOW / HIGHLIGHT: four modes (menu: "Player highlight") -----
		// Every mode draws the part hidden behind a wall with GL_GREATER and the
		// part in plain sight with GL_LEQUAL, exactly like before. What changes
		// is HOW the character is painted:
		if (want_glow)
		{
			switch (s.glow_mode)
			{
			case settings::glow_cloth_only:
				// real clothes, no colour at all: you see the enemy's actual
				// outfit / skin / hair / gun through the wall
				textured_pass(GL_GREATER, st, real_draw);
				break;

			case settings::glow_cloth_tint:
				// real clothes + a light colour laid over them
				textured_pass(GL_GREATER, st, real_draw);
				fill_pass(GL_GREATER, s.glow_through_color, s.glow_alpha * 0.45f, real_draw);
				if (s.glow_open)
					fill_pass(GL_LEQUAL, s.glow_open_color, s.glow_alpha * 0.45f, real_draw);
				break;

			case settings::glow_blend:
				// texture stays readable, the colour is blended over it
				blend_fill(GL_GREATER, s.glow_through_color, s.glow_alpha, real_draw);
				if (s.glow_open)
					blend_fill(GL_LEQUAL, s.glow_open_color, s.glow_alpha, real_draw);
				break;

			case settings::glow_solid:
			default:
				// classic chams: the whole body filled with the colour
				fill_pass(GL_GREATER, s.glow_through_color, s.glow_alpha, real_draw);
				if (s.glow_open)
					fill_pass(GL_LEQUAL, s.glow_open_color, s.glow_alpha, real_draw);
				break;
			}
		}

		// ---- 2b. WIRE: classic 3D wireframe stroke -------------------------
		if (want_wire)
		{
			float wscale = 1.0f;
			if (dist > 0.0f)
				wscale = std::clamp(6.0f / dist, 0.7f, 2.0f);
			const GLfloat wire_w = std::max(1.0f, static_cast<float>(s.wire_size) * wscale);

			// part hidden behind geometry drawn earlier
			wire_pass(GL_GREATER, s.wire_through_color, s.wire_alpha, wire_w, real_draw);

			// part in plain sight
			if (s.wire_open)
				wire_pass(GL_LEQUAL, s.wire_open_color, s.wire_alpha, wire_w, real_draw);
		}

		// ---- 3. STROKE: the v2.3 outline (mask + ring, two colours) ---------
		// mask 0.0 + GL_LESS    = the line in plain sight
		// mask 1.0 + GL_GREATER = the line through walls / cars / bodies
		// The mask is what keeps the copies off the body. Without it the
		// GL_GREATER pass fills the whole character with the stroke colour
		// (the red / magenta blobs of v2.7-v2.9).
		if (want_stroke)
		{
			if (s.stroke_open && band_px_open > 0)
			{
				mask_depth(0.0f, real_draw);
				stroke_ring(GL_LESS, s.stroke_open_color, s.stroke_alpha, band_px_open, real_draw);
			}

			if (band_px_through > 0)
			{
				mask_depth(1.0f, real_draw);
				stroke_ring(GL_GREATER, s.stroke_through_color, s.stroke_alpha, band_px_through, real_draw);
			}

			// put the real depth back where the mask had 0.0 / 1.0 (v3.0 fix for
			// the dark / mask ring: everything drawn after the body gets its own
			// depth back - gun, cloth, hair, the game's rim passes)
			if (st.depth_mask)
				restore_depth(real_draw);
		}

		// ---- 4. DEPTH CLAIM: "see enemies through walls" --------------------
		// Automatic, no percentage. Covered-only in normal play; the strong
		// claim switches itself on while the game's ice wall shader is drawn.
		// This is what makes the chams and the new outline survive the wall.
		if (s.through_walls)
		{
			// Always use the nearest claim (0.0, GL_ALWAYS) when through_walls is
			// on. A glue / ice wall is drawn AFTER the characters and would blend
			// on top of the chams - writing depth 0.0 for the character means the
			// wall loses the depth test on every pixel the chams cover. This is
			// deterministic and does not depend on detecting the wall shader
			// before the character draw (which can fail when the wall is drawn
			// after the character in the same frame).
			//
			// IMPORTANT: claim_depth writes depth 0.0 for the character, which would
			// block every draw that comes after it (gun, cloth, hair, ...). So we
			// immediately restore the real depth. The wall drawn later by the game
			// still loses the depth test because claim_depth ran between the
			// character draw and the wall draw - the wall is the NEXT draw after
			// this draw_chams() returns, and it tests against depth 0.0.
			claim_depth(band_px_through, k_claim_near, GL_ALWAYS, real_draw);
			if (st.depth_mask)
				restore_depth(real_draw);
		}

		gl.DepthRange(st.depth_range[0], st.depth_range[1]);
		restore_state(st);
	}
}
