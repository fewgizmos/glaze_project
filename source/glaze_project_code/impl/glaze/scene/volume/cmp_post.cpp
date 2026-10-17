#include "glp/glaze/scene/volume/cmp_post.h"

#include <cmath>

namespace glaze::scene::vol::component
{

namespace
{

constexpr float EXPOSURE_SCALE = 1.2f;

}

post::post(const post_create_info& create_info) : m_create_info(create_info)
{
    set_flag(component_flags::post);
}

rendering::scene_render::gpu_post post::get_post()
{
    rendering::scene_render::gpu_post result;

    result.exposure   = 1.0f / (EXPOSURE_SCALE * std::exp2(m_create_info.ev100));
    result.tonemapper = static_cast<uint32_t>(m_create_info.tonemapper);

    result.gtao_radius    = m_create_info.gtao_radius;
    result.gtao_intensity = m_create_info.gtao_intensity;

    result.gtao_directions = m_create_info.gtao_directions;
    result.gtao_steps      = m_create_info.gtao_steps;

    result.taa_enabled        = m_create_info.taa_enabled;
    result.taa_history_weight = m_create_info.taa_history_weight;

    return result;
}

}
