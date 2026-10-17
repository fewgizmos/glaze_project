#pragma once

#include <glm/vec3.hpp>

#include <cstdint>

namespace glaze::rendering::scene_render
{

struct gpu_light
{
    glm::vec3 position = glm::vec3(0.0f);
    float range        = 0.0f;

    glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
    float cos_outer     = -1.0f;

    glm::vec3 color = glm::vec3(0.0f);
    uint32_t type   = 0;

    glm::vec3 ground_color = glm::vec3(0.0f);
    float cos_inner        = -1.0f;

    float cos_angular_radius = 1.0f;
    float sin_angular_radius = 0.0f;
    float padding[2]         = { 0.0f, 0.0f };
};

}
