#pragma once

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/volume/cmp_post.h"

namespace glaze::scene::obj
{

class post_object : public scene_entity
{

};

class post_object_prim : public post_object
{
public:
    post_object_prim(const vol::component::post_create_info& create_info);
};

}
