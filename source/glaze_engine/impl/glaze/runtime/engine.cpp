#include "glp/glaze/runtime/engine.h"

#include "glp/glaze/utilities/core/timer.h"

#include "glp/glaze/platform/app.h"
#include "glp/glaze/platform/wm.h"

#include "glp/glaze/io/viewport.h"

#include "glp/glaze/rendering/renderer.h"

#include <cstdlib>
#include <exception>
#include <iostream>

namespace glaze::runtime::engine
{

int run()
{
    try {
        startup();

        while (!wants_stop) {
            tick_frame();
        }

        cleanup();
    }

    catch (const std::exception& error) {
        std::cerr << "engine error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void startup()
{
    platform::app::init();

    io::viewport::setup_for_vk();

    rendering::renderer::init();
}

void cleanup()
{
    rendering::renderer::cleanup();

    io::viewport::destroy();

    platform::wm::cleanup();
    platform::app::cleanup();
}

void tick_frame()
{
    utilities::core::timer::next_frame();

    platform::app::pump_events();
    platform::wm::pump_events_to_windows();

    rendering::renderer::next_frame();

    if (platform::app::wants_exit || io::viewport::get_wants_exit()) {
        wants_stop = true;
    }
}

}
