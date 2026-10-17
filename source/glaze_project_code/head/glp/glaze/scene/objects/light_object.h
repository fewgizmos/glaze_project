#pragma once

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/volume/cmp_light.h"

#include <glm/vec3.hpp>

namespace glaze::scene::obj
{

class light_object : public scene_entity
{

};

class light_object_prim : public light_object
{
public:
    light_object_prim(glm::vec3 pos, glm::vec3 rot, const vol::component::light_create_info& create_info);
};

}
