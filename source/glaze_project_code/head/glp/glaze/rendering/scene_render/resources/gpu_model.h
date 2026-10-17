#pragma once

#include "glp/glaze/rendering/scene_render/resources/materials.h"
#include "glp/glaze/rendering/scene_render/resources/meshes.h"
#include "glp/glaze/rendering/scene_render/resources/models.h"

#include <glm/mat4x4.hpp>

#include <vector>

namespace glaze::rendering::scene_render
{

struct gpu_model_part
{
    const mesh* target_mesh = nullptr;
    const material* target_material = nullptr;

    glm::mat4 transform          = glm::mat4(1.0f);
    glm::mat4 previous_transform = glm::mat4(1.0f);
};

struct gpu_model
{
    std::vector<gpu_model_part> parts;
};

}

namespace glaze::rendering::scene_render::res::gpu_models
{

gpu_model build(const model* target, const glm::mat4& local);

}
