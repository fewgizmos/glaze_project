#include "glp/game/game_management/game_manager.h"

#include "glp/glaze/runtime/engine.h"

#include "glp/glaze/io/input.h"

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/objects/light_object.h"
#include "glp/glaze/scene/objects/post_object.h"
#include "glp/glaze/scene/objects/static_camera_object.h"
#include "glp/glaze/scene/objects/static_object.h"

#include "glp/glaze/world/resources/sky_atmosphere.h"

#include "glp/glaze/utilities/core/timer.h"

#ifndef NDEBUG
#include "glp/trainer/scene/debug_camera.h"
#endif

#include <glm/vec3.hpp>

#include <cmath>

namespace game::game_management::it
{

namespace
{

bool m_is_session_started = false;

glaze::world::res::sky_atmosphere* m_sky = nullptr;

constexpr float DAY_CYCLE_HOURS_PER_SECOND = 0.01f;
constexpr float DAY_HOURS                  = 24.0f;

constexpr const char* FOX_MODEL_PATH = "source/glaze_project_resources/gltf/fox/Fox.gltf";
constexpr const char* TREE_MODEL_PATH = "source/glaze_project_resources/gltf/tree/tree_small_02_2k.gltf";
constexpr const char* GOLD_LOCK_MODEL_PATH = "source/glaze_project_resources/glb/gold_lock.glb";
constexpr const char* WOODEN_CRATE_MODEL_PATH = "source/glaze_project_resources/glb/wooden_crate.glb";
constexpr const char* PCB_MODEL_PATH = "source/glaze_project_resources/gltf/circuit_board/circuit_board_2k.gltf";
constexpr const char* SERVICE_PISTOL_MODEL_PATH = "source/glaze_project_resources/gltf/pistol/service_pistol_2k.gltf";

const glm::vec3 FOX_SCALE = glm::vec3(0.02f);

const glaze::scene::vol::component::light_create_info BLUE_LIGHT = {
    .type      = glaze::scene::vol::component::light_type::point,
    .color     = glm::vec3(0.0f, 0.0f, 1.0f),
    .intensity = 375.0f,
    .range     = 8.0f
};

const glaze::world::res::atmosphere_create_info SUNSET_ATMOSPHERE = {
    .time_of_day                = 18.3f,
    .sun_light_scale            = 1.275f,
    .horizon_light_fade         = 5.0f,
    .horizon_light_twilight     = 10.0f,
    .horizon_light_desaturation = 0.5f,
    .evening_sun_pink           = 5.0f,
    .evening_halo_pink          = 12.0f,
    .moon_light_scale           = 1.5f,
    .moon_overlap_hours         = 1.0f,
    .night_ambient_scale        = 1.2f,
    .twilight_desaturation      = 0.75f,
    .evening_aerosol            = 3.0f,
    .ozone_absorption           = glm::vec3(0.000325f, 0.0009405f, 0.0000425f),
    .cloud_density              = 0.3f
};

const glaze::scene::vol::component::post_create_info POST_SETTINGS = {
    .ev100                  = 13.5f,
    .exposure_compensation  = 1.0f,
    .exposure_speed_up      = 1.0f,
    .bloom_intensity        = 0.08f,
    .light_shafts_intensity = 2.0f
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

    if (m_sky != nullptr) {
        const float time_of_day = m_sky->get_time_of_day() + DAY_CYCLE_HOURS_PER_SECOND * glaze::utilities::core::timer::delta;

        m_sky->set_time_of_day(std::fmod(time_of_day, DAY_HOURS));
    }
}

void init_session()
{
    if (!m_is_session_started) {
        m_is_session_started = true;

        glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(70.0f, 20.0f, 140.0f), FOX_SCALE, FOX_MODEL_PATH);
        glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0), FOX_SCALE, FOX_MODEL_PATH);

        glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(glm::vec3(6.0f, 0.0f, 6.0f), glm::vec3(0), glm::vec3(1.5f), TREE_MODEL_PATH);
        glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(glm::vec3(0.0f, 1.0f, 5.0f), glm::vec3(77.0f, 10.0f, 0.0f), glm::vec3(4.0f), GOLD_LOCK_MODEL_PATH);
        glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(glm::vec3(-8.0f, 0.0f, -6.0f), glm::vec3(0), glm::vec3(3.4f), WOODEN_CRATE_MODEL_PATH);
        glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(glm::vec3(-10.0f, 0.0f, -6.0f), glm::vec3(0), glm::vec3(3.4f), PCB_MODEL_PATH);
        glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(glm::vec3(-6.0f, 0.0f, 6.0f), glm::vec3(0), glm::vec3(3.4f), SERVICE_PISTOL_MODEL_PATH);

        m_sky = glaze::scene::it::create_entity<glaze::world::res::sky_atmosphere>(SUNSET_ATMOSPHERE);

        glaze::scene::it::create_entity<glaze::scene::obj::post_object_prim>(POST_SETTINGS);

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
