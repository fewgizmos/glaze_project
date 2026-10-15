#pragma once

#include "glp/glaze/scene/volume/volume.h"

namespace glaze::scene::vol::component
{

enum class camera_projection
{
    perspective,
    orthographic
};

class camera : public volume_component
{
public:
    camera(
        camera_projection projection = camera_projection::perspective,
        float fov        = 60.0f,
        float ortho_size = 10.0f,
        float near_plane = 0.1f,
        float far_plane  = 100.0f
    );

    rendering::scene_render::gpu_camera get_camera() override;

private:
    camera_projection m_projection;

    float m_fov;
    float m_ortho_size;

    float m_near_plane;
    float m_far_plane;
};

}
