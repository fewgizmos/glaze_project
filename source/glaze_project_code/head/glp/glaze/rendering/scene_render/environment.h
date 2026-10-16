#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace glaze::rendering::scene_render::environment
{

constexpr uint32_t SKY_BINDING         = 0;
constexpr uint32_t IRRADIANCE_BINDING  = 1;
constexpr uint32_t PREFILTERED_BINDING = 2;
constexpr uint32_t BRDF_LUT_BINDING    = 3;

void init_all();
void clean_all();

VkDescriptorSetLayout get_set_layout();
VkDescriptorSet get_default_set();

VkSampler get_cube_sampler();

void write_brdf_lut(VkDescriptorSet set);

}
