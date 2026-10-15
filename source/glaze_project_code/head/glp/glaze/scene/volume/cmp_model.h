#pragma once

#include "glp/glaze/scene/volume/volume.h"

#include "glp/glaze/rendering/scene_render/resources/models.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"
#include "glp/glaze/rendering/scene_render/resources/shaders_stdlib.h"

#include <string>

namespace glaze::scene::vol::component
{

class model : public volume_component
{
public:
    model(const std::string& gltf_path, const rendering::scene_render::shader* shader = rendering::scene_render::std_base_model_shader());

    rendering::scene_render::gpu_model get_model() override;

private:
    rendering::scene_render::model_ref m_model;
};

}
