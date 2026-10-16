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
    float exposure = 1.0f;

    tonemap_operator tonemapper = tonemap_operator::pbr_neutral;
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
