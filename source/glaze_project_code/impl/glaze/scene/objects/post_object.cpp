#include "glp/glaze/scene/objects/post_object.h"

#include "glp/glaze/scene/volume/volume.h"
#include "glp/glaze/scene/volume/cmp_post.h"

#include <memory>

namespace glaze::scene::obj
{

post_object_prim::post_object_prim(const vol::component::post_create_info& create_info)
{
    m_scene_volume().components.push_back(std::make_unique<vol::component::post>(create_info));
}

}
