#pragma once

#include "glp/glaze/rendering/types.h"

namespace glaze::rendering::scene_renderer
{

void init();
void cleanup();

void draw_all(VkCommandBuffer cmd, const swapchain_image_data& target, VkExtent2D extent);

}
