#pragma once

#include "glp/glaze/rendering/vk/resources.h"

namespace glaze::rendering::scene_render::clouds
{

void init_all();
void clean_all();

void handle_all();
void dispatch();

const image& get_output_image();

}
