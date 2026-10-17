#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace glaze::rendering::scene_render
{

constexpr uint32_t ENVIRONMENT_SH_COUNT = 9;

struct gpu_environment
{
    VkDescriptorSet set = VK_NULL_HANDLE;
    float intensity     = 1.0f;

    VkDeviceAddress irradiance_sh = 0;
};

}
