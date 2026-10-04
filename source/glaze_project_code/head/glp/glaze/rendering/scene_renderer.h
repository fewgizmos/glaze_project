#pragma once

#include "glp/glaze/rendering/vk/resources.h"

namespace glaze::rendering::scene_renderer
{

void init();
void cleanup();

void update_all();
void draw_all();

const image& get_depth_image();

}
