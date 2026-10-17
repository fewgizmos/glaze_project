#include "glp/glaze/scene/objects/light_object.h"

#include "glp/glaze/scene/volume/volume.h"
#include "glp/glaze/scene/volume/cmp_light.h"

#include <memory>

namespace glaze::scene::obj
{

light_object_prim::light_object_prim(glm::vec3 pos, glm::vec3 rot, const vol::component::light_create_info& create_info)
{
    m_scene_volume().w_pos = pos;
    m_scene_volume().w_rot = rot;

    m_scene_gizmo_volume().w_pos = pos;
    m_scene_gizmo_volume().w_rot = rot;

    m_scene_volume().components.push_back(std::make_unique<vol::component::light>(create_info));
}

}
