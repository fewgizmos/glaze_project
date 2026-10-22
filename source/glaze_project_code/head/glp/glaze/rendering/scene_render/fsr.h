#pragma once

#include <vulkan/vulkan_core.h>

#include <glm/vec2.hpp>

#include <cstdint>

namespace glaze::rendering::scene_render::fsr
{

enum class upscale_mode : uint32_t
{
    native_aa         = 0,
    quality           = 1,
    balanced          = 2,
    performance       = 3,
    ultra_performance = 4
};

void init_all();
void clean_all();

void handle_all();
void dispatch();

float get_upscale_ratio(uint32_t mode);

VkExtent2D get_render_extent(VkExtent2D display_extent, uint32_t mode);

glm::vec2 get_jitter_offset(uint32_t frame_index, uint32_t render_width, uint32_t display_width);

float get_texture_lod_bias(uint32_t render_width, uint32_t display_width);

void request_reset();

}
