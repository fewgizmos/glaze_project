#pragma once

#include "glp/glaze/rendering/scene_render/resources/shaders.h"

namespace glaze::rendering::scene_render
{

const shader* std_base_model_shader();
const shader* std_base_model_double_sided_shader();

const shader* std_base_model_masked_shader();
const shader* std_base_model_masked_double_sided_shader();

const shader* std_prepass_shader();
const shader* std_prepass_double_sided_shader();

}
