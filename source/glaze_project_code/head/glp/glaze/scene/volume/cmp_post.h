#pragma once

#include "glp/glaze/scene/volume/volume.h"

#include <cstdint>

namespace glaze::scene::vol::component
{

enum class tonemap_operator : uint32_t
{
    pbr_neutral = 0,
    aces        = 1
};

struct post_create_info
{
    float ev100 = 15.0f;

    tonemap_operator tonemapper = tonemap_operator::pbr_neutral;

    float gtao_radius    = 1.0f;
    float gtao_intensity = 1.0f;

    uint32_t gtao_directions = 4;
    uint32_t gtao_steps      = 6;

    bool taa_enabled = true;

    float taa_history_weight = 0.9f;
};

class post : public volume_component
{
public:
    post(const post_create_info& create_info);

    rendering::scene_render::gpu_post get_post() override;

private:
    post_create_info m_create_info;
};

}
