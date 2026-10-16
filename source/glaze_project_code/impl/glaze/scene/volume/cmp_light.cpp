#include "glp/glaze/scene/volume/cmp_light.h"

#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec4.hpp>

#include <algorithm>

namespace glaze::scene::vol::component
{

light::light(const light_create_info& create_info) : m_create_info(create_info)
{
    set_flag(component_flags::light);
}

rendering::scene_render::gpu_light light::get_light()
{
    const glm::mat4 local = compose_transform(get_pos(), get_rot(), glm::vec3(1.0f));

    const float half_angle = std::clamp(m_create_info.cone_angle, 0.0f, 360.0f) * 0.5f;
    const float softness   = std::clamp(m_create_info.cone_softness, 0.0f, 1.0f);

    rendering::scene_render::gpu_light result;

    result.position  = glm::vec3(local[3]);
    result.direction = glm::normalize(glm::vec3(local * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

    result.range = m_create_info.range;

    if (half_angle < 180.0f) {
        result.cos_outer = glm::cos(glm::radians(half_angle));
        result.cos_inner = glm::cos(glm::radians(half_angle * (1.0f - softness)));
    }

    const float radiance_scale = m_create_info.type == light_type::ambient ? 1.0f : glm::pi<float>();

    result.color        = m_create_info.color * m_create_info.intensity * radiance_scale;
    result.ground_color = m_create_info.ground_color * m_create_info.intensity;

    result.type = static_cast<uint32_t>(m_create_info.type);

    return result;
}

}
