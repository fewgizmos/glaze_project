#pragma once

#include "glp/glaze/rendering/types.h"

namespace glaze::rendering::vk::frame
{

void create_swapchain();
void destroy_swapchain();
void recreate_swapchain();

void create_frame_data();

void advance();

const vkb::Swapchain& get_swapchain();

frame_data& get_current_frame();
swapchain_image_data& get_current_image();

uint32_t& get_image_index();

}
