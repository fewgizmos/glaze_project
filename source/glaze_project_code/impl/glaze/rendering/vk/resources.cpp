#define VMA_IMPLEMENTATION

#include "glp/glaze/rendering/vk/resources.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"

#include "glp/glaze/rendering/defines.h"
#include "glp/glaze/rendering/queue_deletor.h"

#include "glp/glaze/utilities/core/assert.h"

#include <algorithm>
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

struct pending_image_upload
{
    buffer staging;
    VkImage target = VK_NULL_HANDLE;
    VkExtent3D extent = {};
    uint32_t mip_levels = 1;
    uint32_t array_layers = 1;
};

VmaAllocator m_allocator = VK_NULL_HANDLE;

std::vector<pending_upload> m_pending_uploads;
std::vector<pending_image_upload> m_pending_image_uploads;

std::vector<used_staging> m_used_staging;

void destroy_all_uploads()
{
    for (pending_upload& upload : m_pending_uploads) {
        destroy_buffer(upload.staging);
    }

    for (pending_image_upload& upload : m_pending_image_uploads) {
        destroy_buffer(upload.staging);
    }

    for (used_staging& used : m_used_staging) {
        destroy_buffer(used.staging);
    }

    m_pending_uploads.clear();
    m_pending_image_uploads.clear();

    m_used_staging.clear();
}

void transition_mips(
    VkCommandBuffer cmd,
    VkImage target,
    uint32_t base_mip,
    uint32_t mip_count,
    uint32_t layer_count,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkPipelineStageFlags2 src_stage,
    VkAccessFlags2 src_access,
    VkPipelineStageFlags2 dst_stage,
    VkAccessFlags2 dst_access
)
{
    VkImageMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = src_stage;
    barrier.srcAccessMask = src_access;
    barrier.dstStageMask = dst_stage;
    barrier.dstAccessMask = dst_access;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = target;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = base_mip;
    barrier.subresourceRange.levelCount = mip_count;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layer_count;

    VkDependencyInfo dependency_info = {};
    dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency_info.imageMemoryBarrierCount = 1;
    dependency_info.pImageMemoryBarriers = &barrier;

    device::get_disp().cmdPipelineBarrier2(cmd, &dependency_info);
}

void flush_image_upload(VkCommandBuffer cmd, const pending_image_upload& upload)
{
    const vkb::DispatchTable& disp = device::get_disp();

    transition_mips(
        cmd,
        upload.target,
        0,
        upload.mip_levels,
        upload.array_layers,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE,
        0,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT
    );

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = upload.array_layers;
    region.imageExtent = upload.extent;

    disp.cmdCopyBufferToImage(
        cmd,
        upload.staging.handle,
        upload.target,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    int32_t mip_width  = static_cast<int32_t>(upload.extent.width);
    int32_t mip_height = static_cast<int32_t>(upload.extent.height);

    for (uint32_t mip = 1; mip < upload.mip_levels; mip++) {
        const int32_t next_width  = std::max(mip_width / 2, 1);
        const int32_t next_height = std::max(mip_height / 2, 1);

        transition_mips(
            cmd,
            upload.target,
            mip - 1,
            1,
            upload.array_layers,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT
        );

        VkImageBlit blit = {};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = mip - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = upload.array_layers;
        blit.srcOffsets[1] = { mip_width, mip_height, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = mip;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = upload.array_layers;
        blit.dstOffsets[1] = { next_width, next_height, 1 };

        disp.cmdBlitImage(
            cmd,
            upload.target,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            upload.target,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blit,
            VK_FILTER_LINEAR
        );

        mip_width  = next_width;
        mip_height = next_height;
    }

    if (upload.mip_levels > 1) {
        transition_mips(
            cmd,
            upload.target,
            0,
            upload.mip_levels - 1,
            upload.array_layers,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
        );
    }

    transition_mips(
        cmd,
        upload.target,
        upload.mip_levels - 1,
        1,
        upload.array_layers,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );
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

void queue_image_upload(const image& target, const void* pixels, VkDeviceSize size)
{
    pending_image_upload upload;

    upload.staging = create_buffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    upload.target     = target.handle;
    upload.extent     = target.extent;
    upload.mip_levels = target.mip_levels;
    upload.array_layers = target.array_layers;

    upload_buffer(upload.staging, pixels, size);

    m_pending_image_uploads.push_back(upload);
}

void flush_uploads()
{
    VkCommandBuffer cmd = frame::get_current_frame().command_buffer;

    for (const pending_image_upload& upload : m_pending_image_uploads) {
        flush_image_upload(cmd, upload);

        m_used_staging.push_back({ upload.staging, MAX_FRAMES_IN_FLIGHT });
    }

    m_pending_image_uploads.clear();

    if (m_pending_uploads.empty()) {
        return;
    }

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

image create_image(
    VkExtent3D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspect,
    uint32_t mip_levels,
    uint32_t array_layers,
    bool is_cube
)
{
    VkImageCreateInfo image_create_info = {};
    image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_create_info.flags = is_cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.format = format;
    image_create_info.extent = extent;
    image_create_info.mipLevels = mip_levels;
    image_create_info.arrayLayers = array_layers;
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
    result_image.mip_levels = mip_levels;
    result_image.array_layers = array_layers;

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
    view_create_info.viewType = is_cube ? VK_IMAGE_VIEW_TYPE_CUBE : (array_layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
    view_create_info.format = format;
    view_create_info.subresourceRange.aspectMask = aspect;
    view_create_info.subresourceRange.baseMipLevel = 0;
    view_create_info.subresourceRange.levelCount = mip_levels;
    view_create_info.subresourceRange.baseArrayLayer = 0;
    view_create_info.subresourceRange.layerCount = array_layers;

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
