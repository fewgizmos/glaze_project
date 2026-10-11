#include "glp/gl_game/game_managment/game_manager.h"

#include "glp/glaze/runtime/engine.h"

#include "glp/glaze/io/input.h"

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/objects/static_object.h"

#include <glm/vec3.hpp>

namespace gl_game::game_managment::it
{

namespace
{

bool m_is_session_started = false;

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

        glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0), "source/glaze_project_resources/gltf/fox/Fox.gltf");
        glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0), "source/glaze_project_resources/gltf/fox/Fox.gltf");
        glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(glm::vec3(6.0f, 0.0f, 6.0f), glm::vec3(0), "source/glaze_project_resources/gltf/fox/Fox.gltf");
    }
}

void cleanup_session()
{
    m_is_session_started = false;

    glaze::scene::it::erase_all_entities_typed<glaze::scene::obj::static_object_model_prim>();
}

}
