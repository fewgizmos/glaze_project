#pragma once

#include "glp/glaze/scene/scene.h"

#include <glm/vec3.hpp>

#include <string>

namespace glaze::scene::obj
{

class static_object : public scene_entity
{

};

class static_object_model_prim : public static_object
{
public:
    static_object_model_prim(glm::vec3 pos, glm::vec3 rot, glm::vec3 scale, const std::string& gltf_path);
};

}
