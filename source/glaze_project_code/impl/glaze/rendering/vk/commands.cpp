#include "glp/glaze/rendering/vk/commands.h"

#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/queue_deletor.h"

#include "glp/glaze/utilities/core/assert.h"

namespace glaze::rendering::vk::commands
{

namespace
{

VkCommandPool m_pool = VK_NULL_HANDLE;

}

void create_pool()
{
    VkCommandPoolCreateInfo pool_create_info = {};
    pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_create_info.queueFamilyIndex = device::get_queues().graphics_family;

    const VkResult result = device::get_disp().createCommandPool(&pool_create_info, nullptr, &m_pool);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create command pool");

    queue_deletor::push([pool = m_pool]() { device::get_disp().destroyCommandPool(pool, nullptr); });
}

VkCommandPool get_pool() { return m_pool; }

VkCommandBuffer allocate()
{
    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = m_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;

    const VkResult result = device::get_disp().allocateCommandBuffers(&alloc_info, &cmd);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to allocate command buffer");

    return cmd;
}

void reset(VkCommandBuffer cmd)
{
    const VkResult result = device::get_disp().resetCommandBuffer(cmd, 0);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to reset command buffer");
}

void begin(VkCommandBuffer cmd, VkCommandBufferUsageFlags flags)
{
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = flags;

    const VkResult result = device::get_disp().beginCommandBuffer(cmd, &begin_info);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to begin command buffer");
}

void end(VkCommandBuffer cmd)
{
    const VkResult result = device::get_disp().endCommandBuffer(cmd);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to record command buffer");
}

void submit(
    VkCommandBuffer cmd,
    const VkSemaphoreSubmitInfo* wait,
    const VkSemaphoreSubmitInfo* signal,
    VkFence fence
)
{
    VkCommandBufferSubmitInfo cmd_info = {};
    cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmd_info.commandBuffer = cmd;

    VkSubmitInfo2 submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.waitSemaphoreInfoCount = wait != nullptr ? 1 : 0;
    submit_info.pWaitSemaphoreInfos = wait;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &cmd_info;
    submit_info.signalSemaphoreInfoCount = signal != nullptr ? 1 : 0;
    submit_info.pSignalSemaphoreInfos = signal;

    const VkResult result = device::get_disp().queueSubmit2(device::get_queues().graphics, 1, &submit_info, fence);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to submit command buffer");
}

void immediate_submit(const std::function<void(VkCommandBuffer)>& record)
{
    const vkb::DispatchTable& disp = device::get_disp();

    VkCommandBuffer cmd = allocate();

    begin(cmd);
    record(cmd);
    end(cmd);

    VkFenceCreateInfo fence_create_info = {};
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence fence = VK_NULL_HANDLE;

    VkResult result = disp.createFence(&fence_create_info, nullptr, &fence);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create immediate submit fence");

    submit(cmd, nullptr, nullptr, fence);

    result = disp.waitForFences(1, &fence, VK_TRUE, UINT64_MAX);

    disp.destroyFence(fence, nullptr);
    disp.freeCommandBuffers(m_pool, 1, &cmd);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to wait for immediate submit");
}

void push_constants(VkCommandBuffer cmd, VkPipelineLayout layout, VkShaderStageFlags stages, uint32_t size, const void* data)
{
    device::get_disp().cmdPushConstants(cmd, layout, stages, 0, size, data);
}

void memory_barrier(
    VkCommandBuffer cmd,
    VkPipelineStageFlags2 src_stage,
    VkAccessFlags2 src_access,
    VkPipelineStageFlags2 dst_stage,
    VkAccessFlags2 dst_access
)
{
    VkMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = src_stage;
    barrier.srcAccessMask = src_access;
    barrier.dstStageMask = dst_stage;
    barrier.dstAccessMask = dst_access;

    VkDependencyInfo dependency_info = {};
    dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency_info.memoryBarrierCount = 1;
    dependency_info.pMemoryBarriers = &barrier;

    device::get_disp().cmdPipelineBarrier2(cmd, &dependency_info);
}

void transition_image(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkPipelineStageFlags2 src_stage,
    VkAccessFlags2 src_access,
    VkPipelineStageFlags2 dst_stage,
    VkAccessFlags2 dst_access,
    VkImageAspectFlags aspect
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
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkDependencyInfo dependency_info = {};
    dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency_info.imageMemoryBarrierCount = 1;
    dependency_info.pImageMemoryBarriers = &barrier;

    device::get_disp().cmdPipelineBarrier2(cmd, &dependency_info);
}

}
