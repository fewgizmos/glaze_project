#pragma once

#include "glp/glaze/rendering/scene_render/resources/gpu_camera.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_light.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_model.h"

#include <glm/mat4x4.hpp>

namespace glaze::scene::render_proxy
{

void proxy_visibles();

namespace util
{

void apply_model_world(rendering::scene_render::gpu_model& target, const glm::mat4& world, const glm::mat4& previous_world);
void apply_camera_world(rendering::scene_render::gpu_camera& target, const glm::mat4& world);
void apply_light_world(rendering::scene_render::gpu_light& target, const glm::mat4& world);

}

}
