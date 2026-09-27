#pragma once

#include <SDL3/SDL_events.h>

#include <vector>

namespace glaze::platform::app
{

inline std::vector<SDL_Event> m_events_buf;
inline bool wants_exit = false;

void init();
void cleanup();

void pump_events();

inline const std::vector<SDL_Event>& get_events() { return m_events_buf; }

}
