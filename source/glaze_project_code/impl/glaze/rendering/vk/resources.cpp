#define VMA_IMPLEMENTATION

#include "glp/glaze/rendering/vk/resources.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"

#include "glp/glaze/rendering/defines.h"
#include "glp/glaze/rendering/queue_deletor.h"

#include "glp/glaze/utilities/core/assert.h"

#include <vector>

namespace glaze::rendering::vk::resources
{

namespace
{

struct pending_upload
{
    buffer staging;
    VkBuffer target = VK_NULL_HANDLE;
};

struct used_staging
{
    buffer staging;
    int frames_left = 0;
};

VmaAllocator m_allocator = VK_NULL_HANDLE;

std::vector<pending_upload> m_pending_uploads;
std::vector<used_staging> m_used_staging;

void destroy_all_uploads()
{
    for (pending_upload& upload : m_pending_uploads) {
        destroy_buffer(upload.staging);
    }

    for (used_staging& used : m_used_staging) {
        destroy_buffer(used.staging);
    }

    m_pending_uploads.clear();
    m_used_staging.clear();
}

}

void create_allocator(const vkb::Instance& instance)
{
    VmaVulkanFunctions functions = {};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo create_info = {};
    create_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    create_info.vulkanApiVersion = VK_API_VERSION_1_3;
    create_info.instance = instance.instance;
    create_info.physicalDevice = device::get_physical().physical_device;
    create_info.device = device::get_device().device;
    create_info.pVulkanFunctions = &functions;

    const VkResult result = vmaCreateAllocator(&create_info, &m_allocator);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create vma allocator");

    queue_deletor::push([allocator = m_allocator]() { vmaDestroyAllocator(allocator); });
    queue_deletor::push([]() { destroy_all_uploads(); });
}

VmaAllocator get_allocator() { return m_allocator; }

buffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags flags)
{
    VkBufferCreateInfo buffer_create_info = {};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = size;
    buffer_create_info.usage = usage;
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_create_info = {};
    alloc_create_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_create_info.flags = flags;

    buffer result_buffer;
    result_buffer.size = size;

    const VkResult result = vmaCreateBuffer(
        m_allocator,
        &buffer_create_info,
        &alloc_create_info,
        &result_buffer.handle,
        &result_buffer.allocation,
        nullptr
    );

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create buffer");

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo address_info = {};
        address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        address_info.buffer = result_buffer.handle;

        result_buffer.address = device::get_disp().getBufferDeviceAddress(&address_info);
    }

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

void queue_upload(const buffer& target, const void* data, VkDeviceSize size)
{
    pending_upload upload;

    upload.staging = create_buffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    upload.target = target.handle;

    upload_buffer(upload.staging, data, size);

    m_pending_uploads.push_back(upload);
}

void flush_uploads()
{
    if (m_pending_uploads.empty()) {
        return;
    }

    VkCommandBuffer cmd = frame::get_current_frame().command_buffer;

    for (const pending_upload& upload : m_pending_uploads) {
        VkBufferCopy region = {};
        region.size = upload.staging.size;

        device::get_disp().cmdCopyBuffer(cmd, upload.staging.handle, upload.target, 1, &region);

        m_used_staging.push_back({ upload.staging, MAX_FRAMES_IN_FLIGHT });
    }

    m_pending_uploads.clear();

    commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COPY_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_MEMORY_READ_BIT
    );
}

void destroy_used_staging()
{
    for (auto it = m_used_staging.begin(); it != m_used_staging.end();) {
        if (it->frames_left > 0) {
            it->frames_left--;
            it++;

            continue;
        }

        destroy_buffer(it->staging);

        it = m_used_staging.erase(it);
    }
}

image create_image(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect)
{
    VkImageCreateInfo image_create_info = {};
    image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.format = format;
    image_create_info.extent = extent;
    image_create_info.mipLevels = 1;
    image_create_info.arrayLayers = 1;
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_create_info.usage = usage;
    image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_create_info = {};
    alloc_create_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_create_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    image result_image;
    result_image.extent = extent;
    result_image.format = format;

    VkResult result = vmaCreateImage(
        m_allocator,
        &image_create_info,
        &alloc_create_info,
        &result_image.handle,
        &result_image.allocation,
        nullptr
    );

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create image");

    VkImageViewCreateInfo view_create_info = {};
    view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_create_info.image = result_image.handle;
    view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_create_info.format = format;
    view_create_info.subresourceRange.aspectMask = aspect;
    view_create_info.subresourceRange.baseMipLevel = 0;
    view_create_info.subresourceRange.levelCount = 1;
    view_create_info.subresourceRange.baseArrayLayer = 0;
    view_create_info.subresourceRange.layerCount = 1;

    result = device::get_disp().createImageView(&view_create_info, nullptr, &result_image.view);

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
