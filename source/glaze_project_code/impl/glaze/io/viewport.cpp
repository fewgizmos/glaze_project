#include "glp/glaze/io/viewport.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_vulkan.h>

namespace glaze::io::viewport
{

void setup_for_vk()
{
    platform::wm::window_create_info create_info;

    create_info.title = "wh";

    create_info.width  = 1920;
    create_info.height = 1080;

    create_info.is_fullscreen = false;
    create_info.is_resizable  = false;

    create_info.is_bordered = false;
    create_info.is_visible  = true;

    create_info.is_cursor_captured = false;

    create_info.extra_sdl_flags |= SDL_WINDOW_VULKAN;

    m_win = platform::wm::create_window(create_info);
}

void destroy()
{
    platform::wm::destroy_window(m_win);

    m_win = nullptr;
}

}
