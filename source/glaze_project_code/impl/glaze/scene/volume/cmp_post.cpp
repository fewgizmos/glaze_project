#include "glp/glaze/scene/volume/cmp_post.h"

namespace glaze::scene::vol::component
{

post::post(const post_create_info& create_info) : m_create_info(create_info)
{
    set_flag(component_flags::post);
}

rendering::scene_render::gpu_post post::get_post()
{
    rendering::scene_render::gpu_post result;

    result.ev100 = m_create_info.ev100;

    result.auto_exposure         = m_create_info.auto_exposure;
    result.exposure_compensation = m_create_info.exposure_compensation;
    result.min_ev100             = m_create_info.min_ev100;
    result.max_ev100             = m_create_info.max_ev100;
    result.exposure_speed_up     = m_create_info.exposure_speed_up;
    result.exposure_speed_down   = m_create_info.exposure_speed_down;

    result.tonemapper = static_cast<uint32_t>(m_create_info.tonemapper);
    result.sharpen    = m_create_info.sharpen;

    result.bloom_intensity = m_create_info.bloom_intensity;

    result.light_shafts_intensity = m_create_info.light_shafts_intensity;

    result.gtao_radius    = m_create_info.gtao_radius;
    result.gtao_intensity = m_create_info.gtao_intensity;

    result.gtao_directions = m_create_info.gtao_directions;
    result.gtao_steps      = m_create_info.gtao_steps;

    result.taa_enabled        = m_create_info.taa_enabled;
    result.taa_history_weight = m_create_info.taa_history_weight;

    return result;
}

}
