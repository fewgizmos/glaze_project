#include "glp/glaze/scene/scene_render_proxy.h"

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/volume/volume.h"

#include "glp/glaze/rendering/scene_render/frame_pipeline.h"

#include "glp/glaze/rendering/scene_render/resources/gpu_camera.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_light.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_model.h"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace glaze::scene::render_proxy
{

namespace
{

struct world_record
{
    glm::mat4 world;
    uint32_t frame;
};

std::unordered_map<const vol::scene_volume*, world_record> m_worlds;

rendering::scene_render::gpu_model m_model;

uint32_t m_frame = 0;

void proxy_camera()
{
    const scene_entity* player = it::get_player_entity();

    if (player == nullptr) {
        return;
    }

    const vol::scene_volume& volume = player->scene_volume();

    for (const std::unique_ptr<vol::volume_component>& comp : volume.components) {
        if (!comp->has_flag(vol::component_flags::camera)) {
            continue;
        }

        rendering::scene_render::gpu_camera camera = comp->get_camera();

        util::apply_camera_world(
            camera,
            vol::compose_transform(volume.w_pos, volume.w_rot, glm::vec3(1.0f))
        );

        rendering::scene_render::frame_pipe::set_camera(camera);

        return;
    }
}

void proxy_volume_models(const vol::scene_volume& volume)
{
    const glm::mat4 world = vol::compose_transform(volume.w_pos, volume.w_rot, volume.w_scale);

    const auto [record, inserted] = m_worlds.try_emplace(&volume, world_record{ world, m_frame });

    const glm::mat4 previous_world = inserted ? world : record->second.world;

    record->second = { world, m_frame };

    for (const std::unique_ptr<vol::volume_component>& comp : volume.components) {
        if (!comp->has_flag(vol::component_flags::modeled)) {
            continue;
        }

        m_model.parts.clear();

        comp->get_model(m_model);

        util::apply_model_world(m_model, world, previous_world);

        rendering::scene_render::frame_pipe::submit_model(m_model);
    }
}

bool proxy_volume_post(const vol::scene_volume& volume)
{
    for (const std::unique_ptr<vol::volume_component>& comp : volume.components) {
        if (!comp->has_flag(vol::component_flags::post)) {
            continue;
        }

        rendering::scene_render::frame_pipe::set_post(comp->get_post());

        return true;
    }

    return false;
}

bool proxy_volume_atmosphere(const vol::scene_volume& volume)
{
    for (const std::unique_ptr<vol::volume_component>& comp : volume.components) {
        if (!comp->has_flag(vol::component_flags::atmosphere)) {
            continue;
        }

        rendering::scene_render::frame_pipe::set_atmosphere(comp->get_atmosphere());

        return true;
    }

    return false;
}

bool proxy_volume_environment(const vol::scene_volume& volume)
{
    for (const std::unique_ptr<vol::volume_component>& comp : volume.components) {
        if (!comp->has_flag(vol::component_flags::environment)) {
            continue;
        }

        rendering::scene_render::frame_pipe::set_environment(comp->get_environment());

        return true;
    }

    return false;
}

void proxy_volume_lights(const vol::scene_volume& volume)
{
    const glm::mat4 world = vol::compose_transform(volume.w_pos, volume.w_rot, glm::vec3(1.0f));

    for (const std::unique_ptr<vol::volume_component>& comp : volume.components) {
        if (!comp->has_flag(vol::component_flags::light)) {
            continue;
        }

        rendering::scene_render::gpu_light light = comp->get_light();

        util::apply_light_world(light, world);

        rendering::scene_render::frame_pipe::submit_light(light);
    }
}

}

void proxy_visibles()
{
    rendering::scene_render::frame_pipe::clear();

    proxy_camera();

    bool has_post        = false;
    bool has_environment = false;
    bool has_atmosphere  = false;

    for (const std::unique_ptr<scene_entity>& ent : it::get_all_entities()) {
        if (it::util::is_entity_pending_erase(ent.get())) {
            continue;
        }

        proxy_volume_models(ent->scene_volume());
        proxy_volume_models(ent->scene_gizmo_volume());

        proxy_volume_lights(ent->scene_volume());
        proxy_volume_lights(ent->scene_gizmo_volume());

        if (!has_post) {
            has_post = proxy_volume_post(ent->scene_volume()) || proxy_volume_post(ent->scene_gizmo_volume());
        }

        if (!has_environment) {
            has_environment = proxy_volume_environment(ent->scene_volume()) || proxy_volume_environment(ent->scene_gizmo_volume());
        }

        if (!has_atmosphere) {
            has_atmosphere = proxy_volume_atmosphere(ent->scene_volume()) || proxy_volume_atmosphere(ent->scene_gizmo_volume());
        }
    }

    std::erase_if(m_worlds, [](const auto& entry) { return entry.second.frame != m_frame; });

    m_frame++;
}

namespace util
{

void apply_model_world(rendering::scene_render::gpu_model& target, const glm::mat4& world, const glm::mat4& previous_world)
{
    for (rendering::scene_render::gpu_model_part& part : target.parts) {
        part.previous_transform = previous_world * part.previous_transform;
        part.transform          = world * part.transform;
    }
}

void apply_camera_world(rendering::scene_render::gpu_camera& target, const glm::mat4& world)
{
    target.view     = target.view * glm::inverse(world);
    target.position = glm::vec3(world * glm::vec4(target.position, 1.0f));

    target.view_proj     = target.proj * target.view;
    target.inv_view_proj = glm::inverse(target.view_proj);
}

void apply_light_world(rendering::scene_render::gpu_light& target, const glm::mat4& world)
{
    target.position  = glm::vec3(world * glm::vec4(target.position, 1.0f));
    target.direction = glm::normalize(glm::vec3(world * glm::vec4(target.direction, 0.0f)));
}

}

}
