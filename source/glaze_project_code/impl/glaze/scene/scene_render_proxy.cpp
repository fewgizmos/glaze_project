#include "glp/glaze/scene/scene_render_proxy.h"

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/volume/volume.h"

#include "glp/glaze/rendering/scene_render/frame_pipeline.h"

#include "glp/glaze/rendering/scene_render/resources/gpu_camera.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_light.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_model.h"

#include <memory>

namespace glaze::scene::render_proxy
{

namespace
{

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

        rendering::scene_render::res::gpu_cameras::apply_world(
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

    for (const std::unique_ptr<vol::volume_component>& comp : volume.components) {
        if (!comp->has_flag(vol::component_flags::modeled)) {
            continue;
        }

        rendering::scene_render::gpu_model model = comp->get_model();

        rendering::scene_render::res::gpu_models::apply_world(model, world);

        rendering::scene_render::frame_pipe::submit_model(model);
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

        rendering::scene_render::res::gpu_lights::apply_world(light, world);

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
    }
}

}
