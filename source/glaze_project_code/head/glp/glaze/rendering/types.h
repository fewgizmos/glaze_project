#pragma once

#include <vulkan/vulkan_core.h>
#include <vkb/VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <string>
#include <vector>

namespace glaze::rendering
{

struct queue_set
{
    VkQueue graphics = VK_NULL_HANDLE;
    VkQueue present  = VK_NULL_HANDLE;

    uint32_t graphics_family = 0;
    uint32_t present_family  = 0;
};

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

struct swapchain_state
{
    vkb::Swapchain handle;
    std::vector<swapchain_image_data> images;
};

struct buffer
{
    VkBuffer handle = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

struct image
{
    VkImage handle = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkExtent3D extent = {};
    VkFormat format = VK_FORMAT_UNDEFINED;
};

enum class shader_stage
{
    vertex,
    fragment,
    compute
};

struct graphics_pipeline_info
{
    std::string vert_file;
    std::string frag_file;

    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;

    VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace front_face    = VK_FRONT_FACE_CLOCKWISE;
};

struct pipeline
{
    VkPipeline handle = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

}
