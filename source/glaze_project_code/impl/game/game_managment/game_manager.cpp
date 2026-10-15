#include "glp/game/game_managment/game_manager.h"

#include "glp/glaze/runtime/engine.h"

#include "glp/glaze/io/input.h"

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/objects/game_object.h"
#include "glp/glaze/scene/objects/static_object.h"

#include <glm/vec3.hpp>

namespace game::game_managment::it
{

namespace
{

bool m_is_session_started = false;

constexpr const char* FOX_MODEL_PATH = "source/glaze_project_resources/gltf/fox/Fox.gltf";

const glm::vec3 FOX_SCALE = glm::vec3(0.02f);

}

void statup()
{

}

void shutdown()
{

}

void update_frame()
{
    if (glaze::io::input::key_is_pressed(glaze::io::input::keyboard_input_codes::esc)) {
        glaze::runtime::engine::wants_stop = true;
    }

    if (glaze::io::input::key_is_pressed(glaze::io::input::keyboard_input_codes::p)) {
        if (!m_is_session_started) {
            init_session();
        } else {
            cleanup_session();
        }
    }
}

void init_session()
{
    if (!m_is_session_started) {
        m_is_session_started = true;

        glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(70.0f, 20.0f, 140.0f), FOX_SCALE, FOX_MODEL_PATH);
        glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0), FOX_SCALE, FOX_MODEL_PATH);
        glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(glm::vec3(6.0f, 0.0f, 6.0f), glm::vec3(0), FOX_SCALE, FOX_MODEL_PATH);

        if (glaze::scene::it::get_player_entity() == nullptr) {
            glaze::scene::it::set_player_entity(
                glaze::scene::it::create_entity<glaze::scene::obj::game_object_static_camera_prim>(glm::vec3(2.5f, 3.0f, 2.5f), glm::vec3(-40.0f, 45.0f, 0.0f))
            );
        }
    }
}

void cleanup_session()
{
    m_is_session_started = false;

    glaze::scene::it::erase_all_entities_typed<glaze::scene::obj::static_object_model_prim>();
}

}
