#pragma once

#include "glp/glaze/platform/wm.h"

#include <glm/vec2.hpp>

namespace glaze::io::viewport
{

inline platform::wm::window* m_win = nullptr;

void setup_for_vk();
void destroy();

inline SDL_Window* get_native() { return platform::wm::get_native(m_win); }
inline glm::ivec2 get_extent()  { return platform::wm::get_extent(m_win); }

inline bool get_wants_exit() { return m_win->m_wants_close; }

inline bool get_unhandle_resized() { return m_win->m_unhandle_resized; }
inline void clear_unhandle_resized() { m_win->m_unhandle_resized = false; }

}
