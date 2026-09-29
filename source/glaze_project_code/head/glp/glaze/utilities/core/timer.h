#pragma once

#include <chrono>

namespace glaze::utilities::core::timer
{

void next_frame();

inline std::chrono::high_resolution_clock m_timer{};

inline std::chrono::high_resolution_clock::time_point m_last_frame_end = m_timer.now();
inline std::chrono::high_resolution_clock::time_point m_frame_start    = m_timer.now();

inline float delta    = 0;
inline float delta_ms = 0;
inline float fps      = 0;

}
