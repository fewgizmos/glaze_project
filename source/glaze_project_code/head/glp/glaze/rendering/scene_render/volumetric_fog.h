#pragma once

#include "glp/glaze/rendering/vk/resources.h"

namespace glaze::rendering::scene_render::volumetric_fog
{

void init_all();
void clean_all();

void handle_all();
void dispatch();

bool is_active();

VkDeviceAddress get_params_address();

const image& get_volume_image();

}
