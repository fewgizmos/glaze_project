#pragma once

#include <vulkan/vulkan_core.h>

#include <glm/vec4.hpp>

#include <cstdint>

namespace glaze::rendering::scene_render
{

constexpr uint32_t ENVIRONMENT_SH_COUNT = 9;

struct gpu_environment
{
    VkDescriptorSet set = VK_NULL_HANDLE;
    float intensity     = 1.0f;

    glm::vec4 irradiance_sh[ENVIRONMENT_SH_COUNT] = {};
};

}
