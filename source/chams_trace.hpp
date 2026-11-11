#pragma once

// Bounded, opt-in draw-wrapper accounting. This does not query or change GL
// state and does not identify game objects. Each instance is owned by one thread.
#include <cstdio>

namespace reshade::chams
{
	struct frame_trace
	{
		bool active = false;
		unsigned frame = 0, lines = 0;
		const void *context = nullptr;
		int detect = 0, renderer = 0, hide_own = 0;
		unsigned calls = 0, disabled = 0, menu = 0, budget = 0, primitive = 0;
		unsigned filtered = 0, matched = 0, own_hidden = 0, no_effect = 0;
		unsigned direct = 0, overlay = 0, overlay_failed = 0, no_info = 0;
		float own_est = -1.0f;
		int own_stable = 0;

		template <typename Emit>
		bool enter(bool enabled, const void *ctx, unsigned id, int mode, int render,
		           bool hide, Emit emit)
		{
			if (!enabled || ctx == nullptr)
			{
				// End the activation without a partial-frame summary. No writes or
				// allocations occur when diagnostics are disabled.
				if (active) *this = frame_trace{};
				return false;
			}
			if (!active || frame != id || context != ctx)
			{
				if (active && lines < 1800)
				{
					char line[768];
					std::snprintf(line, sizeof(line),
						"[CHAMS-FRAME] f=%u ctx=%p detect=%d render=%d hideown=%d calls=%u disabled=%u menu=%u budget=%u primitive=%u filtered=%u matched=%u hidden=%u no_effect=%u no_info=%u direct=%u overlay=%u overlay_failed=%u ownest=%.3f stable=%d",
						frame, context, detect, renderer, hide_own, calls, disabled, menu,
						budget, primitive, filtered, matched, own_hidden, no_effect,
						no_info, direct, overlay, overlay_failed, own_est, own_stable);
					emit(line);
					++lines;
				}
				const unsigned emitted = lines;
				*this = frame_trace{};
				lines = emitted;
				active = true; frame = id; context = ctx;
				detect = mode; renderer = render; hide_own = hide ? 1 : 0;
			}
			++calls;
			return true;
		}
	};
}
