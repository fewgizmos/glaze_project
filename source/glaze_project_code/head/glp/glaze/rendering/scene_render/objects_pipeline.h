#pragma once

#include <vulkan/vulkan_core.h>

namespace glaze::rendering::scene_render::objects_pipeline
{

void init_all();
void clean_all();

void handle_all();
void draw_all();

VkDeviceAddress get_objects_address();
VkDeviceAddress get_materials_address();

}
