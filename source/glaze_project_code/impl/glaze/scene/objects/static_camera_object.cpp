#include "glp/glaze/scene/objects/static_camera_object.h"

#include "glp/glaze/scene/volume/volume.h"
#include "glp/glaze/scene/volume/cmp_camera.h"

#include <memory>

namespace glaze::scene::obj
{

static_camera_object_prim::static_camera_object_prim(glm::vec3 pos, glm::vec3 rot)
{
    m_scene_volume().w_pos = pos;
    m_scene_volume().w_rot = rot;

    m_scene_volume().components.push_back(std::make_unique<vol::component::camera>());
}

}
