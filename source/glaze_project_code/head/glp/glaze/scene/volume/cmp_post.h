#pragma once

#include "glp/glaze/scene/volume/volume.h"

namespace glaze::scene::vol::component
{

struct post_create_info
{
    float exposure = 1.0f;
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
