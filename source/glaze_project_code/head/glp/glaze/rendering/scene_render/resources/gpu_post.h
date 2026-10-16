#pragma once

#include <cstdint>

namespace glaze::rendering::scene_render
{

struct gpu_post
{
    float exposure = 1.0f;

    uint32_t tonemapper = 0;
};

}
