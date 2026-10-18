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

    result.exposure_night_darkening = m_create_info.exposure_night_darkening;
    result.exposure_center_weight   = m_create_info.exposure_center_weight;

    result.exposure_incident_metering = m_create_info.exposure_incident_metering;

    result.gpu_budget_fraction = m_create_info.gpu_budget_fraction;

    result.tonemapper         = static_cast<uint32_t>(m_create_info.tonemapper);
    result.tonemap_contrast   = m_create_info.tonemap_contrast;
    result.tonemap_saturation = m_create_info.tonemap_saturation;
    result.sharpen            = m_create_info.sharpen;

    result.bloom_intensity = m_create_info.bloom_intensity;

    result.light_shafts_intensity = m_create_info.light_shafts_intensity;
    result.sun_glare_intensity    = m_create_info.sun_glare_intensity;
    result.night_vision_intensity = m_create_info.night_vision_intensity;

    result.gtao_radius    = m_create_info.gtao_radius;
    result.gtao_intensity = m_create_info.gtao_intensity;

    result.gtao_directions = m_create_info.gtao_directions;
    result.gtao_steps      = m_create_info.gtao_steps;

    result.taa_enabled        = m_create_info.taa_enabled;
    result.taa_history_weight = m_create_info.taa_history_weight;

    result.shadows_enabled    = m_create_info.shadows_enabled;
    result.shadow_lod_bias    = m_create_info.shadow_lod_bias;
    result.shadow_softness    = m_create_info.shadow_softness;
    result.shadow_normal_bias = m_create_info.shadow_normal_bias;

    result.contact_shadow_length = m_create_info.contact_shadow_length;

    result.volumetric_fog_enabled  = m_create_info.volumetric_fog_enabled;
    result.volumetric_fog_distance = m_create_info.volumetric_fog_distance;

    return result;
}

}
