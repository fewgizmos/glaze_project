#pragma once

#include "glp/glaze/scene/scene.h"

#include <glm/vec3.hpp>

namespace glaze::scene::obj
{

class static_camera_object : public scene_entity
{

};

class static_camera_object_prim : public static_camera_object
{
public:
    static_camera_object_prim(glm::vec3 pos, glm::vec3 rot);
};

}
