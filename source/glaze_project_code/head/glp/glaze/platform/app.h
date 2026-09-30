#pragma once

#include <SDL3/SDL_events.h>

#include <vector>

namespace glaze::platform::app
{

inline bool wants_exit = false;

void init();
void cleanup();

void pump_events();

const std::vector<SDL_Event>& get_events();

}
