#pragma once

#include "glp/glaze/rendering/types.h"

#include <vector>

namespace glaze::rendering::vk::descriptors
{

VkDescriptorPool create_pool(uint32_t max_sets, const std::vector<VkDescriptorPoolSize>& sizes);
void destroy_pool(VkDescriptorPool pool);

VkDescriptorSetLayout create_set_layout(const std::vector<VkDescriptorSetLayoutBinding>& bindings);
void destroy_set_layout(VkDescriptorSetLayout layout);

VkDescriptorSet allocate_set(VkDescriptorPool pool, VkDescriptorSetLayout layout);

void write_image(
    VkDescriptorSet set,
    uint32_t binding,
    VkDescriptorType type,
    VkImageView view,
    VkImageLayout layout,
    VkSampler sampler = VK_NULL_HANDLE
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
