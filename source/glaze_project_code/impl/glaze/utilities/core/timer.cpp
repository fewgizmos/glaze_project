#include "glp/glaze/utilities/core/timer.h"

#include <algorithm>
#include <thread>
#include <chrono>

namespace glaze::utilities::core::timer
{

namespace
{

std::chrono::high_resolution_clock m_timer{};

std::chrono::high_resolution_clock::time_point m_last_frame_end = m_timer.now();
std::chrono::high_resolution_clock::time_point m_frame_start    = m_timer.now();

using ms = std::chrono::duration<float, std::milli>;

constexpr int   MAX_FPS   = 80;
constexpr float MAX_DELTA = 0.1f;

constexpr float MIN_FRAME_MS = 1000.0f / MAX_FPS;

}

void next_frame()
{
    m_frame_start = m_timer.now();
    delta_ms = std::chrono::duration_cast<ms>(m_frame_start - m_last_frame_end).count();

    if (delta_ms < MIN_FRAME_MS) {
        std::this_thread::sleep_for(ms(MIN_FRAME_MS - delta_ms));
        m_frame_start = m_timer.now();
        delta_ms = std::chrono::duration_cast<ms>(m_frame_start - m_last_frame_end).count();
    }

    m_last_frame_end = m_frame_start;

    fps = 1000.0f / delta_ms;

    delta = std::min(delta_ms / 1000.0f, MAX_DELTA);
    delta_ms = delta * 1000.0f;

    timestamp += delta;
}

}
