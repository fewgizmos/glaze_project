#pragma once

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <glm/vec2.hpp>

#include <memory>
#include <string>
#include <vector>

namespace glaze::platform::wm
{

struct window_create_info
{
    std::string title = "Glaze";

    int width  = 1280;
    int height = 720;

    bool is_fullscreen = false;
    bool is_resizable  = true;

    bool is_bordered = true;
    bool is_visible  = true;

    bool is_cursor_captured = false;

    SDL_WindowFlags extra_sdl_flags = 0;
};

struct window
{
    SDL_Window* m_handle = nullptr;

    bool m_wants_close      = false;
    bool m_unhandle_resized = false;
};

window* create_window(const window_create_info& create_info);
void destroy_window(window* win);

window* get_window_by_id(SDL_WindowID id);

void pump_events_to_windows();

void cleanup();

glm::ivec2 get_extent(window* win);

void capture_cursor(window* win);
void release_cursor(window* win);

inline SDL_Window* get_native(window* win) { return win->m_handle; }
inline SDL_WindowID get_id(window* win) { return SDL_GetWindowID(win->m_handle); }

inline bool is_cursor_captured(window* win) { return SDL_GetWindowRelativeMouseMode(win->m_handle); }

}
