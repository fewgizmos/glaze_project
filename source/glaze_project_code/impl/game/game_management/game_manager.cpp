#include "glp/game/game_management/game_manager.h"

#include "glp/glaze/runtime/engine.h"

#include "glp/glaze/io/input.h"

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/objects/game_object.h"
#include "glp/glaze/scene/objects/static_object.h"

#include "glp/glaze/world/resources/cubemap.h"

#ifndef NDEBUG
#include "glp/trainer/scene/debug_camera.h"
#endif

#include <glm/vec3.hpp>

namespace game::game_management::it
{

namespace
{

bool m_is_session_started = false;

constexpr const char* FOX_MODEL_PATH = "source/glaze_project_resources/gltf/fox/Fox.gltf";
constexpr const char* PALMS_MODEL_PATH = "source/glaze_project_resources/glb/palms.glb";

constexpr const char* SKY_CUBEMAP_DIR = "source/glaze_project_resources/cubemaps/sky";

const glm::vec3 FOX_SCALE = glm::vec3(0.02f);

const glaze::scene::vol::component::light_create_info SUN_LIGHT = {
    .type      = glaze::scene::vol::component::light_type::directional,
    .color     = glm::vec3(1.0f, 0.95f, 0.85f),
    .intensity = 0.41f
};

const glaze::scene::vol::component::light_create_info AMBIENT_LIGHT = {
    .type         = glaze::scene::vol::component::light_type::ambient,
    .color        = glm::vec3(0.35f, 0.45f, 0.6f),
    .ground_color = glm::vec3(0.15f, 0.12f, 0.1f),
    .intensity    = 0.85f
};

const glaze::scene::vol::component::light_create_info BLUE_LIGHT = {
    .type      = glaze::scene::vol::component::light_type::point,
    .color     = glm::vec3(0.0f, 0.0f, 1.0f),
    .intensity = 14.0f,
    .range     = 8.0f
};

const glaze::scene::vol::component::post_create_info POST_SETTINGS = {
    .exposure = 1.0f
};

}

void startup()
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
        glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(glm::vec3(6.0f, 0.0f, 6.0f), glm::vec3(0), glm::vec3(1.5f), PALMS_MODEL_PATH);

        glaze::scene::it::create_entity<glaze::world::res::cubemap>(SKY_CUBEMAP_DIR);

        glaze::scene::it::create_entity<glaze::scene::obj::game_object_post_prim>(POST_SETTINGS);

        glaze::scene::it::create_entity<glaze::scene::obj::game_object_light_prim>(glm::vec3(0.0f), glm::vec3(-32.0f, -116.0f, 0.0f), SUN_LIGHT);
        glaze::scene::it::create_entity<glaze::scene::obj::game_object_light_prim>(glm::vec3(0.0f), glm::vec3(0.0f), AMBIENT_LIGHT);

        glaze::scene::it::create_entity<glaze::scene::obj::game_object_light_prim>(glm::vec3(1.4f, 1.0f, 0.0f), glm::vec3(0.0f), BLUE_LIGHT);

        if (glaze::scene::it::get_player_entity() == nullptr) {
#ifndef NDEBUG
            glaze::scene::it::set_player_entity(glaze::scene::it::create_entity<trainer::scene::debug_camera_player_obj>());
#else
            glaze::scene::it::set_player_entity(
                glaze::scene::it::create_entity<glaze::scene::obj::game_object_static_camera_prim>(glm::vec3(2.5f, 3.0f, 2.5f), glm::vec3(-40.0f, 45.0f, 0.0f))
            );
#endif
        }
    }
}

void cleanup_session()
{
    m_is_session_started = false;

    glaze::scene::it::erase_all_entities_typed<glaze::scene::obj::static_object_model_prim>();
    glaze::scene::it::erase_all_entities_typed<glaze::world::res::cubemap>();
    glaze::scene::it::erase_all_entities_typed<glaze::scene::obj::game_object_post_prim>();
    glaze::scene::it::erase_all_entities_typed<glaze::scene::obj::game_object_light_prim>();
}

}
