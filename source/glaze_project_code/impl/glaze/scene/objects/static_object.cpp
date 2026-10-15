#include "glp/glaze/scene/objects/static_object.h"

#include "glp/glaze/scene/volume/volume.h"
#include "glp/glaze/scene/volume/cmp_model.h"

#include <memory>

namespace glaze::scene::obj
{

static_object_model_prim::static_object_model_prim(glm::vec3 pos, glm::vec3 rot, glm::vec3 scale, const std::string& gltf_path, bool is_gizmo)
{
    m_scene_volume().w_pos   = pos;
    m_scene_volume().w_rot   = rot;
    m_scene_volume().w_scale = scale;

    m_scene_gizmo_volume().w_pos   = pos;
    m_scene_gizmo_volume().w_rot   = rot;
    m_scene_gizmo_volume().w_scale = scale;

    std::unique_ptr<vol::component::model> model_comp = std::make_unique<vol::component::model>(gltf_path);

    if (is_gizmo) {
        m_scene_gizmo_volume().components.push_back(std::move(model_comp));
    } else {
        m_scene_volume().components.push_back(std::move(model_comp));
    }
}

}
