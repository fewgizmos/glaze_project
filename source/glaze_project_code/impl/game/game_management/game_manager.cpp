#include "glp/game/game_management/game_manager.h"

#include "glp/glaze/runtime/engine.h"

#include "glp/glaze/io/input.h"

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/objects/light_object.h"
#include "glp/glaze/scene/objects/post_object.h"
#include "glp/glaze/scene/objects/static_camera_object.h"
#include "glp/glaze/scene/objects/static_object.h"

#include "glp/glaze/world/resources/skybox.h"

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
constexpr const char* GOLD_LOCK_MODEL_PATH = "source/glaze_project_resources/glb/gold_lock.glb";
constexpr const char* WOODEN_CRATE_MODEL_PATH = "source/glaze_project_resources/glb/wooden_crate.glb";

constexpr const char* SKYBOX_PATH = "source/glaze_project_resources/skybox/sky_v3.hdr";

const glm::vec3 FOX_SCALE = glm::vec3(0.02f);

const glaze::scene::vol::component::light_create_info AMBIENT_LIGHT = {
    .type         = glaze::scene::vol::component::light_type::ambient,
    .color        = glm::vec3(0.35f, 0.45f, 0.6f),
    .ground_color = glm::vec3(0.15f, 0.12f, 0.1f),
    .intensity    = 0.70f
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
        glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(glm::vec3(0.0f, 1.0f, 5.0f), glm::vec3(77.0f, 10.0f, 0.0f), glm::vec3(4.0f), GOLD_LOCK_MODEL_PATH);
        glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(glm::vec3(-8.0f, 0.0f, -6.0f), glm::vec3(0), glm::vec3(3.4f), WOODEN_CRATE_MODEL_PATH);

        glaze::scene::it::create_entity<glaze::world::res::skybox>(SKYBOX_PATH, 0.4f, 1.5f);

        glaze::scene::it::create_entity<glaze::scene::obj::post_object_prim>(POST_SETTINGS);

        glaze::scene::it::create_entity<glaze::scene::obj::light_object_prim>(glm::vec3(0.0f), glm::vec3(0.0f), AMBIENT_LIGHT);

        glaze::scene::it::create_entity<glaze::scene::obj::light_object_prim>(glm::vec3(1.4f, 1.0f, 0.0f), glm::vec3(0.0f), BLUE_LIGHT);

        if (glaze::scene::it::get_player_entity() == nullptr) {
#ifndef NDEBUG
            glaze::scene::it::set_player_entity(glaze::scene::it::create_entity<trainer::scene::debug_camera_player_obj>());
#else
            glaze::scene::it::set_player_entity(
                glaze::scene::it::create_entity<glaze::scene::obj::static_camera_object_prim>(glm::vec3(2.5f, 3.0f, 2.5f), glm::vec3(-40.0f, 45.0f, 0.0f))
            );
#endif
        }
    }
}

void cleanup_session()
{
    m_is_session_started = false;

    glaze::scene::it::erase_all_entities_typed<glaze::scene::obj::static_object_model_prim>();
}

}
