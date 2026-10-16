#pragma once

#include "glp/glaze/scene/volume/volume.h"

#include <glm/vec3.hpp>

#include <cstdint>

namespace glaze::scene::vol::component
{

enum class light_type : uint32_t
{
    directional = 0,
    point       = 1,
    ambient     = 2
};

struct light_create_info
{
    light_type type = light_type::point;

    glm::vec3 color        = glm::vec3(1.0f);
    glm::vec3 ground_color = glm::vec3(0.0f);

    float intensity = 1.0f;

    float range = 10.0f;

    float cone_angle    = 360.0f;
    float cone_softness = 0.2f;
};

class light : public volume_component
{
public:
    light(const light_create_info& create_info);

    rendering::scene_render::gpu_light get_light() override;

private:
    light_create_info m_create_info;
};

}
