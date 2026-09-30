#pragma once

#include "glp/glaze/rendering/types.h"

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
