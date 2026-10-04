#pragma once

#include <vulkan/vulkan_core.h>
#include <vkb/VkBootstrap.h>

#include <cstdint>

namespace glaze::rendering
{

struct frame_data
{
    VkFence fence_inflight = VK_NULL_HANDLE;
    VkSemaphore semaphore_available = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
};

struct swapchain_image_data
{
    VkSemaphore semaphore_finished = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
};

}

namespace glaze::rendering::vk::frame
{

void create_swapchain();
void destroy_swapchain();
void recreate_swapchain();

void create_frame_data();

void advance();

const vkb::Swapchain& get_swapchain();

frame_data& get_current_frame();
swapchain_image_data& get_current_image();

uint32_t& get_image_index();

uint32_t get_current_frame_index();

}
