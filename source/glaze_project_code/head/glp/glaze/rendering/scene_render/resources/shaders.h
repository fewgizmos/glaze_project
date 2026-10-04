#pragma once

#include "glp/glaze/rendering/vk/shaders.h"

#include <string>
#include <vector>

namespace glaze::rendering::scene_render
{

struct shader
{
    pipeline graphics;
    pipeline compute;
};

}

namespace glaze::rendering::scene_render::resources::shaders
{

const shader* build(
    const std::string& name,
    const std::string& vert_file,
    const std::string& frag_file,
    const std::string& comp_file,
    const graphics_pipeline_create_info& graphics_create_info,
    const compute_pipeline_create_info& compute_create_info
);

const shader* get(const std::string& name);

std::vector<std::string> get_all_names();

void erase_all();

void destroy_replaced();

}
