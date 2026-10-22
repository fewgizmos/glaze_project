#include "glp/glaze/runtime/engine.h"

#include "glp/glaze/utilities/core/timer.h"

#include "glp/glaze/platform/app.h"
#include "glp/glaze/platform/window_manager.h"

#include "glp/glaze/io/input.h"
#include "glp/glaze/io/viewport.h"

#include "glp/game/game_management/game_manager.h"

#ifndef NDEBUG
#include "glp/trainer/core/trainer.h"
#endif

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/scene_render_proxy.h"

#include "glp/glaze/rendering/renderer.h"
#include "glp/glaze/rendering/imgui_render/imgui_render.h"

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
        std::cerr << error.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void startup()
{
    platform::app::init();

    io::viewport::setup_for_vk();

    rendering::it::init();

    game::game_management::it::startup();
    game::game_management::it::init_session();

#ifndef NDEBUG
    trainer::core::it::startup();
#endif
}

void cleanup()
{
#ifndef NDEBUG
    trainer::core::it::cleanup();
#endif

    game::game_management::it::cleanup_session();
    game::game_management::it::shutdown();

    scene::it::erase_all_entities();
    scene::it::apply_pending_ops();

    rendering::it::cleanup();

    io::viewport::destroy();

    platform::wm::cleanup();
    platform::app::cleanup();
}

void tick_frame()
{
    utilities::core::timer::next_frame();

    platform::app::pump_events();
    platform::wm::pump_events_to_windows();

    rendering::imgui_render::begin_frame();

    io::input::parse_frame_inputs();

    game::game_management::it::update_frame();

#ifndef NDEBUG
    trainer::core::it::frame_update();
#endif

    scene::it::update_entities();
    scene::render_proxy::proxy_visibles();

    rendering::imgui_render::end_frame();
    rendering::it::render_frame();

    scene::it::apply_pending_ops();

    if (platform::app::wants_exit || io::viewport::get_wants_exit()) {
        wants_stop = true;
    }
}

}
