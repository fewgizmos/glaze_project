#pragma once

#include <vulkan/vulkan_core.h>
#include <vkb/VkBootstrap.h>

#include <cstdint>

namespace glaze::rendering
{

struct queue_set
{
    VkQueue graphics = VK_NULL_HANDLE;
    VkQueue present  = VK_NULL_HANDLE;

    uint32_t graphics_family = 0;
    uint32_t present_family  = 0;
};

}

namespace glaze::rendering::vk::device
{

void create_surface(const vkb::Instance& instance);
void create_device(const vkb::Instance& instance);

void make_queues();

const vkb::PhysicalDevice& get_physical();
const vkb::Device& get_device();
const vkb::DispatchTable& get_disp();

VkSurfaceKHR get_surface();

const queue_set& get_queues();

bool has_memory_budget();

const VkPhysicalDeviceIDProperties& get_id_properties();

}
