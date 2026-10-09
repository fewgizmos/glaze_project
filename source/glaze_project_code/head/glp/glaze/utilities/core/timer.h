#pragma once

namespace glaze::utilities::core::timer
{

void next_frame();

inline float timestamp = 0;

inline float delta    = 0;
inline float delta_ms = 0;
inline float fps      = 0;

}
