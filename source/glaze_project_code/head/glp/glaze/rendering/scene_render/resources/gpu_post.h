#pragma once

#include <cstdint>

namespace glaze::rendering::scene_render
{

struct gpu_post
{
    float exposure = 1.0f;

    uint32_t tonemapper = 0;

    float gtao_radius    = 1.0f;
    float gtao_intensity = 1.0f;

    uint32_t gtao_directions = 4;
    uint32_t gtao_steps      = 6;

    bool taa_enabled = true;

    float taa_history_weight = 0.9f;
};

}
