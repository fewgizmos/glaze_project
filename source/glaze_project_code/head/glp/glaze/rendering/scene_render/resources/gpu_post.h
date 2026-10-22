#pragma once

#include <cstdint>

namespace glaze::rendering::scene_render
{

struct gpu_post
{
    float ev100 = 15.0f;

    bool auto_exposure = true;

    float exposure_compensation = 1.0f;
    float min_ev100             = -6.0f;
    float max_ev100             = 18.0f;
    float exposure_speed_up     = 3.0f;
    float exposure_speed_down   = 1.0f;

    float exposure_night_darkening = 0.6f;
    float exposure_center_weight   = 8.0f;

    bool exposure_incident_metering = true;

    float gpu_budget_fraction = 0.75f;

    uint32_t tonemapper = 2;

    float tonemap_contrast   = 1.2f;
    float tonemap_saturation = 1.2f;

    float sharpen = 0.5f;

    float bloom_intensity = 0.04f;

    float light_shafts_intensity = 1.0f;
    float sun_glare_intensity    = 1.0f;

    float night_vision_intensity = 1.0f;

    float gtao_radius    = 1.0f;
    float gtao_intensity = 1.0f;

    uint32_t gtao_directions = 4;
    uint32_t gtao_steps      = 6;

    uint32_t upscale_mode = 2;

    bool shadows_enabled = true;

    float shadow_lod_bias    = 0.0f;
    float shadow_softness    = 1.0f;
    float shadow_normal_bias = 1.5f;

    float contact_shadow_length = 0.25f;

    bool volumetric_fog_enabled = true;

    float volumetric_fog_distance = 100.0f;
};

}
