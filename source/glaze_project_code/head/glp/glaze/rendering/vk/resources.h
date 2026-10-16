#pragma once

#include <vulkan/vulkan_core.h>
#include <vkb/VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <cstdint>

namespace glaze::rendering
{

struct buffer
{
    VkBuffer handle = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkDeviceAddress address = 0;
};

struct image
{
    VkImage handle = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkExtent3D extent = {};
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t mip_levels = 1;
    uint32_t array_layers = 1;
};

}

namespace glaze::rendering::vk::resources
{

void create_allocator(const vkb::Instance& instance);

VmaAllocator get_allocator();

buffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags flags = 0);
void destroy_buffer(buffer& target);

void upload_buffer(const buffer& target, const void* data, VkDeviceSize size);

void queue_upload(const buffer& target, const void* data, VkDeviceSize size);
void queue_image_upload(const image& target, const void* pixels, VkDeviceSize size);

void record_image_upload(VkCommandBuffer cmd, const image& target, const buffer& staging);
void record_mip_generation(VkCommandBuffer cmd, const image& target);

void flush_uploads();
void destroy_used_staging();

image create_image(
    VkExtent3D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspect,
    uint32_t mip_levels = 1,
    uint32_t array_layers = 1,
    bool is_cube = false
);

void destroy_image(image& target);

VkImageView create_image_view(const image& target, VkImageViewType view_type, uint32_t base_mip, uint32_t mip_count);
void destroy_image_view(VkImageView view);

}
