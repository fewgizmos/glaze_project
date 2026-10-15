#pragma once

#include "glp/glaze/scene/scene.h"

#include <glm/vec3.hpp>

namespace glaze::scene::obj
{

class game_object : public scene_entity
{

};

class game_object_static_camera_prim : public game_object
{
public:
    game_object_static_camera_prim(glm::vec3 pos, glm::vec3 rot);
};

}
