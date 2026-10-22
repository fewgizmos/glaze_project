#pragma once

#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>

namespace glaze::rendering
{

constexpr uint32_t PROFILER_MAX_MARKS = 32;

struct gpu_timings
{
    std::array<const char*, PROFILER_MAX_MARKS> names = {};
    std::array<float, PROFILER_MAX_MARKS> ms = {};

    uint32_t count = 0;

    float total_ms = 0.0f;
};

}

namespace glaze::rendering::vk::profiler
{

void create();
void destroy();

void begin_frame(VkCommandBuffer cmd);
void mark(VkCommandBuffer cmd, const char* name);

const gpu_timings& get_timings();

}
