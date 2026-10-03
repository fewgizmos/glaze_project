#pragma once

#include <vulkan/vulkan_core.h>
#include <vkb/VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

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
};

struct vertex
{
    glm::vec3 pos;
    glm::vec3 normal;
};

struct mesh
{
    buffer vertex_buffer;
    buffer index_buffer;
    uint32_t index_count = 0;
};

}

namespace glaze::rendering::vk::resources
{

void create_allocator(const vkb::Instance& instance);

VmaAllocator get_allocator();

buffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags flags = 0);
void destroy_buffer(buffer& target);

void upload_buffer(const buffer& target, const void* data, VkDeviceSize size);

buffer create_device_buffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);

mesh create_mesh(const std::vector<vertex>& vertices, const std::vector<uint32_t>& indices);
void destroy_mesh(mesh& target);

image create_image(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect);
void destroy_image(image& target);

}
