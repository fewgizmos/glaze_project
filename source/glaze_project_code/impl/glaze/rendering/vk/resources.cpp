#define VMA_IMPLEMENTATION

#include "glp/glaze/rendering/vk/resources.h"

#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/queue_deletor.h"

#include "glp/glaze/utilities/core/assert.h"

namespace glaze::rendering::vk::resources
{

namespace
{

VmaAllocator m_allocator = VK_NULL_HANDLE;

}

void create_allocator(const vkb::Instance& instance)
{
    VmaVulkanFunctions functions = {};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo create_info = {};
    create_info.vulkanApiVersion = VK_API_VERSION_1_3;
    create_info.instance = instance.instance;
    create_info.physicalDevice = device::get_physical().physical_device;
    create_info.device = device::get_device().device;
    create_info.pVulkanFunctions = &functions;

    const VkResult result = vmaCreateAllocator(&create_info, &m_allocator);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create vma allocator");

    queue_deletor::push([allocator = m_allocator]() { vmaDestroyAllocator(allocator); });
}

VmaAllocator get_allocator() { return m_allocator; }

buffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags flags)
{
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = flags;

    buffer result_buffer;
    result_buffer.size = size;

    const VkResult result = vmaCreateBuffer(
        m_allocator,
        &buffer_info,
        &alloc_info,
        &result_buffer.handle,
        &result_buffer.allocation,
        nullptr
    );

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create buffer");

    return result_buffer;
}

void destroy_buffer(buffer& target)
{
    vmaDestroyBuffer(m_allocator, target.handle, target.allocation);

    target = {};
}

void upload_buffer(const buffer& target, const void* data, VkDeviceSize size)
{
    GLAZE_ASSERT(size > target.size, "upload exceeds buffer size");

    const VkResult result = vmaCopyMemoryToAllocation(m_allocator, data, target.allocation, 0, size);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to upload buffer");
}

image create_image(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect)
{
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = extent;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    image result_image;
    result_image.extent = extent;
    result_image.format = format;

    VkResult result = vmaCreateImage(
        m_allocator,
        &image_info,
        &alloc_info,
        &result_image.handle,
        &result_image.allocation,
        nullptr
    );

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create image");

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = result_image.handle;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = aspect;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    result = device::get_disp().createImageView(&view_info, nullptr, &result_image.view);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create image view");

    return result_image;
}

void destroy_image(image& target)
{
    device::get_disp().destroyImageView(target.view, nullptr);

    vmaDestroyImage(m_allocator, target.handle, target.allocation);

    target = {};
}

}
