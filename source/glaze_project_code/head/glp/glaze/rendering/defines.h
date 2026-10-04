#pragma once

#include <vulkan/vulkan_core.h>

namespace glaze::rendering
{

constexpr int MAX_FRAMES_IN_FLIGHT = 4;

constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;

constexpr const char* SHADERS_DIR = "source/glaze_project_resources/shaders/";

#ifdef NDEBUG
constexpr bool GRID_IS_ACTIVE = false;
#else
constexpr bool GRID_IS_ACTIVE = true;
#endif

}
