#include "glp/glaze/scene/objects/game_object.h"

#include "glp/glaze/scene/volume/volume.h"
#include "glp/glaze/scene/volume/cmp_camera.h"

#include <memory>

namespace glaze::scene::obj
{

game_object_static_camera_prim::game_object_static_camera_prim(glm::vec3 pos, glm::vec3 rot)
{
    m_scene_volume().w_pos = pos;
    m_scene_volume().w_rot = rot;

    m_scene_gizmo_volume().w_pos = pos;
    m_scene_gizmo_volume().w_rot = rot;

    m_scene_volume().components.push_back(std::make_unique<vol::component::camera>());
}

game_object_light_prim::game_object_light_prim(glm::vec3 pos, glm::vec3 rot, const vol::component::light_create_info& create_info)
{
    m_scene_volume().w_pos = pos;
    m_scene_volume().w_rot = rot;

    m_scene_gizmo_volume().w_pos = pos;
    m_scene_gizmo_volume().w_rot = rot;

    m_scene_volume().components.push_back(std::make_unique<vol::component::light>(create_info));
}

}
