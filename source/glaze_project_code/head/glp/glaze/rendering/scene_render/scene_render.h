#pragma once

#include "glp/glaze/rendering/vk/resources.h"

namespace glaze::rendering::scene_render
{

void init();
void cleanup();

void handle_all();
void draw_all();

const image& get_depth_image();

VkDeviceAddress get_frame_data_address();

}
