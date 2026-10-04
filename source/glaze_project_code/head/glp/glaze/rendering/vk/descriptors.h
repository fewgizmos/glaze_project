#pragma once

#include "glp/glaze/rendering/vk/resources.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <vector>

namespace glaze::rendering::vk::descriptors
{

VkDescriptorPool create_pool(
    uint32_t max_sets,
    const std::vector<VkDescriptorPoolSize>& sizes,
    VkDescriptorPoolCreateFlags flags = 0
);

void destroy_pool(VkDescriptorPool pool);

VkDescriptorSetLayout create_set_layout(
    const std::vector<VkDescriptorSetLayoutBinding>& bindings,
    const std::vector<VkDescriptorBindingFlags>& binding_flags = {}
);

void destroy_set_layout(VkDescriptorSetLayout layout);

VkDescriptorSet allocate_set(VkDescriptorPool pool, VkDescriptorSetLayout layout);

void write_image(
    VkDescriptorSet set,
    uint32_t binding,
    VkDescriptorType type,
    VkImageView view,
    VkImageLayout layout,
    VkSampler sampler = VK_NULL_HANDLE,
    uint32_t element = 0
);

void write_buffer(
    VkDescriptorSet set,
    uint32_t binding,
    VkDescriptorType type,
    const buffer& target,
    VkDeviceSize offset = 0,
    VkDeviceSize range = VK_WHOLE_SIZE
);

}
