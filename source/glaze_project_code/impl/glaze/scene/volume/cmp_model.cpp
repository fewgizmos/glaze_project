#include "glp/glaze/scene/volume/cmp_model.h"

namespace glaze::scene::vol::component
{

model::model(const std::string& gltf_path, const rendering::scene_render::shader* shader) : m_model(gltf_path, shader)
{
    set_flag(component_flags::modeled);
}

void model::get_model(rendering::scene_render::gpu_model& out)
{
    rendering::scene_render::res::gpu_models::build(
        m_model.get_handle(),
        compose_transform(get_pos(), get_rot(), get_scale()),
        out
    );
}

}
