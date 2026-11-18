/*
 * fps_counter.hpp -- FPS counter logic (Stage 97)
 */
#pragma once
#include <chrono>
#include <string>
#include <utility>
#include <algorithm>

namespace reshade::chams
{
    enum class fps_position_t : int { TopLeft=0, TopRight=1, BottomLeft=2, BottomRight=3 };
    inline std::string fps_position_to_string_fps(int pos)
    {
        switch(pos){ case 1: return "TopRight"; case 2: return "BottomLeft"; case 3: return "BottomRight"; default: return "TopLeft"; }
    }
    inline int fps_position_from_string_fps(const std::string &s)
    {
        if(s=="TopRight") return 1;
        if(s=="BottomLeft") return 2;
        if(s=="BottomRight") return 3;
        return 0;
    }
    struct fps_counter_state
    {
        double update_interval = 0.5;
        double elapsed = 0.0;
        unsigned frame_count = 0;
        double displayed_fps = 0.0;
        double last_frame_time = 0.0;
        bool initialized = false;
    };
    inline void fps_counter_reset(fps_counter_state &st)
    {
        st.elapsed = 0.0;
        st.frame_count = 0;
        st.displayed_fps = 0.0;
        st.last_frame_time = 0.0;
        st.initialized = false;
    }
    inline bool fps_counter_update(fps_counter_state &st, double delta_seconds)
    {
        if (delta_seconds <= 0.0) delta_seconds = 1e-6;
        if (delta_seconds > 1.0) delta_seconds = 1.0;
        st.elapsed += delta_seconds;
        st.frame_count++;
        bool updated = false;
        if (st.elapsed >= st.update_interval)
        {
            double fps = static_cast<double>(st.frame_count) / st.elapsed;
            if (!st.initialized)
            {
                st.displayed_fps = fps;
                st.initialized = true;
            }
            else
            {
                st.displayed_fps = st.displayed_fps * 0.7 + fps * 0.3;
            }
            st.elapsed = 0.0;
            st.frame_count = 0;
            updated = true;
        }
        st.last_frame_time = delta_seconds;
        return updated;
    }
    inline double fps_counter_get(const fps_counter_state &st)
    {
        return st.displayed_fps;
    }
    inline std::pair<float,float> fps_position_for_viewport(int vp_width, int vp_height, int pos, float text_width, float text_height, float margin = 10.0f)
    {
        if (vp_width <=0) vp_width = 1920;
        if (vp_height <=0) vp_height = 1080;
        float x=margin, y=margin;
        if (pos % 2 == 1)
            x = static_cast<float>(vp_width) - text_width - margin;
        if (pos > 1)
            y = static_cast<float>(vp_height) - text_height - margin;
        x = std::max(margin, std::min(x, static_cast<float>(vp_width) - text_width - margin));
        y = std::max(margin, std::min(y, static_cast<float>(vp_height) - text_height - margin));
        return {x,y};
    }
    inline bool fps_should_render(bool show_fps_enabled)
    {
        return show_fps_enabled;
    }
}
