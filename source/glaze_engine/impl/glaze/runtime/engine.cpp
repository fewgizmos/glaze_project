#include "glp/glaze/runtime/engine.h"

#include "glp/glaze/platform/app.h"
// #include "gmx/gizmox_platform/viewport.h"
#include "glp/glaze/platform/wm.h"

// #include "gmx/gizmox_rhi_vk/vk_context.h"

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

    // gizmox_platform::viewport::setup_vk();

    // gizmox_rhi_vk::context::init();
}

void cleanup()
{
    // gizmox_rhi_vk::context::cleanup();

    // gizmox_platform::viewport::destroy();

    platform::wm::cleanup();
    platform::app::cleanup();
}

void tick_frame()
{
    platform::app::pump_events();
    platform::wm::pump_events_to_windows();

    // gizmox_rhi_vk::context::draw_frame();

    if (platform::app::wants_exit) { // || io::viewport::get_wants_exit()
        wants_stop = true;
    }
}

}
