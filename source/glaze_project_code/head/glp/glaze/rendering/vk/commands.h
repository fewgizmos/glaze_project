#pragma once

#include "glp/glaze/rendering/vk/shaders.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <functional>

namespace glaze::rendering::vk::commands
{

void create_pool();

VkCommandPool get_pool();

VkCommandBuffer allocate();

void reset(VkCommandBuffer cmd);

void begin(VkCommandBuffer cmd, VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
void end(VkCommandBuffer cmd);

void submit(
    VkCommandBuffer cmd,
    const VkSemaphoreSubmitInfo* wait,
    const VkSemaphoreSubmitInfo* signal,
    VkFence fence
);

void immediate_submit(const std::function<void(VkCommandBuffer)>& record);

void push_constants(VkCommandBuffer cmd, VkPipelineLayout layout, VkShaderStageFlags stages, uint32_t size, const void* data);

void dispatch(
    VkCommandBuffer cmd,
    const pipeline& target,
    const void* push,
    uint32_t push_size,
    uint32_t groups_x,
    uint32_t groups_y,
    uint32_t groups_z
);

void memory_barrier(
    VkCommandBuffer cmd,
    VkPipelineStageFlags2 src_stage,
    VkAccessFlags2 src_access,
    VkPipelineStageFlags2 dst_stage,
    VkAccessFlags2 dst_access
);

void transition_image(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkPipelineStageFlags2 src_stage,
    VkAccessFlags2 src_access,
    VkPipelineStageFlags2 dst_stage,
    VkAccessFlags2 dst_access,
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT,
    uint32_t mip_count = 1,
    uint32_t layer_count = 1
);

}
