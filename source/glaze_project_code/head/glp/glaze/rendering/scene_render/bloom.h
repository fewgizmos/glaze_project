#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace glaze::rendering::scene_render::bloom
{

void init_all();
void clean_all();

void handle_all();
void dispatch();

VkImageView get_output_view();
uint32_t get_mip_count();

}
