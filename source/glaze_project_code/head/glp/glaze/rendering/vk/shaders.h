#pragma once

#include "glp/glaze/rendering/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace glaze::rendering::vk::shaders
{

std::vector<uint32_t> compile_glsl(const std::string& source, shader_stage stage, const std::string& name);

VkShaderModule create_module(const std::vector<uint32_t>& spirv);
void destroy_module(VkShaderModule module);

VkShaderModule load_module(const std::string& file_name);

pipeline create_graphics_pipeline(const graphics_pipeline_info& info);
pipeline create_compute_pipeline(const compute_pipeline_info& info);

void destroy_pipeline(const pipeline& target);

}
