#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace glaze::rendering
{

constexpr int MAX_FRAMES_IN_FLIGHT = 4;

constexpr uint32_t MAX_SAMPLERS = 64;
constexpr uint32_t MAX_TEXTURES = 4096;

constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
constexpr VkFormat HDR_FORMAT   = VK_FORMAT_R16G16B16A16_SFLOAT;

constexpr VkFormat GBUFFER_NORMAL_FORMAT   = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat GBUFFER_VELOCITY_FORMAT = VK_FORMAT_R16G16_SFLOAT;

constexpr VkFormat GTAO_FORMAT = VK_FORMAT_R32_SFLOAT;

constexpr const char* SHADERS_DIR = "source/glaze_project_resources/shaders/";

}
