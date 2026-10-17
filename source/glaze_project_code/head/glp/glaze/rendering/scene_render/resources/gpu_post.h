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

    uint32_t tonemapper = 0;

    float sharpen = 0.5f;

    float bloom_intensity = 0.04f;

    float light_shafts_intensity = 1.0f;

    float gtao_radius    = 1.0f;
    float gtao_intensity = 1.0f;

    uint32_t gtao_directions = 4;
    uint32_t gtao_steps      = 6;

    bool taa_enabled = true;

    float taa_history_weight = 0.9f;
};

}
