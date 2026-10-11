#include "glp/glaze/scene/volume/cmp_model.h"

namespace glaze::scene::vol::component
{

model::model(const std::string& gltf_path, const rendering::scene_render::shader* shader) : m_model(gltf_path, shader) {};

}
