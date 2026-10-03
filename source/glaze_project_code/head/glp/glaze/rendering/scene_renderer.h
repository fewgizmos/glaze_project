#pragma once

#include <vulkan/vulkan_core.h>

namespace glaze::rendering::scene_renderer
{

void init();
void cleanup();

void update_all(VkCommandBuffer cmd);
void draw_all(VkCommandBuffer cmd, VkExtent2D extent);

}
