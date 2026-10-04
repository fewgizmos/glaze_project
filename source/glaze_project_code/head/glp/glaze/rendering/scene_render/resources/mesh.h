#pragma once

#include "glp/glaze/rendering/vk/resources.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <vector>

namespace glaze::rendering::scene_render
{

struct vertex
{
    glm::vec3 pos;
    float uv_x;
    glm::vec3 normal;
    float uv_y;
    glm::vec4 tangent;
    glm::vec4 color;
};

struct mesh
{
    buffer vertex_buffer;
    buffer index_buffer;

    uint32_t vertex_count = 0;
    uint32_t index_count  = 0;

    glm::vec3 bounds_min = glm::vec3(0.0f);
    glm::vec3 bounds_max = glm::vec3(0.0f);
};

}

namespace glaze::rendering::scene_render::resources::meshes
{

const mesh* build(const std::vector<vertex>& vertices, const std::vector<uint32_t>& indices);

void erase(const mesh* target);
void erase_all();

void destroy_erased();

}
