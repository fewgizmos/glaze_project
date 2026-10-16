#pragma once

#include "glp/glaze/rendering/vk/shaders.h"

#include <string>
#include <vector>

namespace glaze::rendering::scene_render
{

enum class render_queue
{
    opaque,
    skybox,
    blended
};

struct shader
{
    pipeline graphics;
    pipeline compute;

    render_queue queue = render_queue::opaque;

    bool uses_environment = false;
};

}

namespace glaze::rendering::scene_render::res::shaders
{

const shader* build(
    const std::string& name,
    const std::string& vert_file,
    const std::string& frag_file,
    const std::string& comp_file,
    const graphics_pipeline_create_info& graphics_create_info,
    const compute_pipeline_create_info& compute_create_info,
    render_queue queue
);

const shader* get(const std::string& name);

std::vector<std::string> get_all_names();

void erase_all();

void destroy_replaced();

}
