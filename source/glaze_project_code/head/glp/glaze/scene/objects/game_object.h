#pragma once

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/volume/cmp_light.h"

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

class game_object_light_prim : public game_object
{
public:
    game_object_light_prim(glm::vec3 pos, glm::vec3 rot, const vol::component::light_create_info& create_info);
};

}
