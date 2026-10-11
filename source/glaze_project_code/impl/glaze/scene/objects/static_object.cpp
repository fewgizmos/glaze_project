#include "glp/glaze/scene/objects/static_object.h"

#include "glp/glaze/scene/volume/volume.h"
#include "glp/glaze/scene/volume/cmp_model.h"

namespace glaze::scene::obj
{

namespace
{

void push_component_to_vol(vol::scene_volume* volume, vol::volume_component* comp) { volume->components.push_back(comp); }

}

static_object_model_prim::static_object_model_prim(glm::vec3 pos, glm::vec3 rot, const std::string& gltf_path, bool is_gizmo)
{
    m_scene_volume().w_pos = pos;
    m_scene_volume().w_rot = rot;

    m_scene_gizmo_volume().w_pos = pos;
    m_scene_gizmo_volume().w_rot = rot;

    vol::component::model model_comp(gltf_path);

    if (is_gizmo) {
        push_component_to_vol(&m_scene_gizmo_volume(), &model_comp);
    } else {
        push_component_to_vol(&m_scene_volume(), &model_comp);
    }
}

}
