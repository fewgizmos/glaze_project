#include "glp/glaze/platform/app.h"

#include <SDL3/SDL_init.h>

namespace glaze::platform::app
{

void init()
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
}

void cleanup()
{
    m_events_buf.clear();
    wants_exit = false;

    SDL_Quit();
}

void pump_events()
{
    m_events_buf.clear();

    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            wants_exit = true;
        }

        m_events_buf.push_back(event);
    }
}

}
