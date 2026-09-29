#include "glp/glaze/platform/window_manager.h"

#include "glp/glaze/platform/app.h"
#include "glp/glaze/utilities/core/assert.h"

#include <SDL3/SDL_error.h>

#include <algorithm>

namespace glaze::platform::wm
{

window* create_window(const window_create_info& create_info)
{
    SDL_WindowFlags flags = SDL_WINDOW_HIDDEN | create_info.extra_sdl_flags;

    if (create_info.is_fullscreen) { flags |= SDL_WINDOW_FULLSCREEN; }
    if (create_info.is_resizable)  { flags |= SDL_WINDOW_RESIZABLE;  }
    if (!create_info.is_bordered)  { flags |= SDL_WINDOW_BORDERLESS; }

    SDL_Window* handle = SDL_CreateWindow(
        create_info.title.c_str(),
        create_info.width,
        create_info.height,
        flags
    );

    GLAZE_ASSERT(handle == nullptr, SDL_GetError());

    SDL_SetWindowPosition(handle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    m_windows.push_back(std::make_unique<window>());

    window* created = m_windows.back().get();
    created->m_handle = handle;

    if (create_info.is_cursor_captured) {
        capture_cursor(created);
    }

    if (create_info.is_visible) {
        SDL_ShowWindow(handle);
    }

    return created;
}

void destroy_window(window* win)
{
    if (win == nullptr) {
        return;
    }

    const auto stored = std::find_if(
        m_windows.begin(),
        m_windows.end(),
        [win](const std::unique_ptr<window>& candidate) { return candidate.get() == win; }
    );

    if (stored == m_windows.end()) {
        return;
    }

    SDL_DestroyWindow(win->m_handle);

    m_windows.erase(stored);
}

window* get_window_by_id(SDL_WindowID id)
{
    for (const std::unique_ptr<window>& stored : m_windows) {
        if (get_id(stored.get()) == id) {
            return stored.get();
        }
    }

    return nullptr;
}

void pump_events_to_windows()
{
    for (const SDL_Event& event : app::get_events()) {
        if (event.type < SDL_EVENT_WINDOW_FIRST || event.type > SDL_EVENT_WINDOW_LAST) {
            continue;
        }

        window* win = get_window_by_id(event.window.windowID);

        if (win == nullptr) {
            continue;
        }

        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            win->m_wants_close = true;
        }

        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            win->m_unhandle_resized = true;
        }
    }
}

void cleanup()
{
    for (const std::unique_ptr<window>& stored : m_windows) {
        SDL_DestroyWindow(stored->m_handle);
    }

    m_windows.clear();
}

glm::ivec2 get_extent(window* win)
{
    glm::ivec2 result(0);

    SDL_GetWindowSizeInPixels(win->m_handle, &result.x, &result.y);

    return result;
}

void capture_cursor(window* win)
{
    const bool captured = SDL_SetWindowRelativeMouseMode(win->m_handle, true);

    GLAZE_ASSERT(!captured, SDL_GetError());
}

void release_cursor(window* win)
{
    const bool released = SDL_SetWindowRelativeMouseMode(win->m_handle, false);

    GLAZE_ASSERT(!released, SDL_GetError());
}

}
