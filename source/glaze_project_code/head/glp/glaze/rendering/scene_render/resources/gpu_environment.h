#pragma once

#include <vulkan/vulkan_core.h>

namespace glaze::rendering::scene_render
{

struct gpu_environment
{
    VkDescriptorSet set = VK_NULL_HANDLE;
    float intensity     = 1.0f;
};

}
