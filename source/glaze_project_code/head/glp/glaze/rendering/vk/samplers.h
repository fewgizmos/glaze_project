#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace glaze::rendering
{

struct sampler_create_info
{
    VkFilter mag_filter = VK_FILTER_LINEAR;
    VkFilter min_filter = VK_FILTER_LINEAR;

    VkSamplerMipmapMode mip_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VkSamplerAddressMode wrap_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode wrap_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode wrap_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    bool anisotropy = true;
};

}

namespace glaze::rendering::vk::samplers
{

uint32_t make(const sampler_create_info& create_info);

VkSampler get(uint32_t index);

uint32_t get_count();

void destroy_all();

}
