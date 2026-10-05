#pragma once

#include "glp/glaze/rendering/scene_render/resources/materials.h"
#include "glp/glaze/rendering/scene_render/resources/meshes.h"
#include "glp/glaze/rendering/scene_render/resources/shaders.h"
#include "glp/glaze/rendering/scene_render/resources/textures.h"

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace glaze::rendering::scene_render
{

struct model_part
{
    const mesh* target_mesh = nullptr;
    const material* target_material = nullptr;
};

struct model_node
{
    int parent = -1;

    glm::mat4 local = glm::mat4(1.0f);

    std::vector<model_part> parts;
};

struct model
{
    std::string path;

    const shader* target_shader = nullptr;

    std::vector<model_node> nodes;

    std::vector<const mesh*> meshes;
    std::vector<const material*> materials;
    std::vector<const texture*> textures;

    uint32_t references = 0;
};

}

namespace glaze::rendering::scene_render::resources::models
{

const model* make(const std::string& gltf_path, const shader* target_shader = nullptr);

void set_unused(const model* target);

void erase_unused();
void erase_all();

}
