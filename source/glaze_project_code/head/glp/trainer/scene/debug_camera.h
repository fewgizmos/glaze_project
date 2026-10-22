#pragma once

#ifndef NDEBUG

#include "glp/glaze/scene/scene.h"

namespace trainer::scene
{

class debug_camera_player_obj : public glaze::scene::scene_entity
{
public:
    debug_camera_player_obj();

    void update() override;

    float get_speed() const     { return m_speed; }
    void set_speed(float speed) { m_speed = speed; }

private:
    float m_speed;
};

}

#endif
