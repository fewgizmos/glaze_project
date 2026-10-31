#include "glp/glaze/io/viewport.h"

#include "glp/glaze/runtime/config.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_vulkan.h>

namespace glaze::io::viewport
{

void setup_for_vk()
{
    platform::wm::window_create_info create_info;

    create_info.title     = RT_GLAZE_WINDOW_NAME;
    create_info.icon_path = RT_GLAZE_WINDOW_LOGO;

    create_info.width  = 1920;
    create_info.height = 1080;

    create_info.is_fullscreen = true;
    create_info.is_resizable  = true;

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
