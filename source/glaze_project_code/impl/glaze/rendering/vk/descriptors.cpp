#include "glp/glaze/rendering/vk/descriptors.h"

#include "glp/glaze/rendering/vk/device.h"

#include "glp/glaze/utilities/core/assert.h"

namespace glaze::rendering::vk::descriptors
{

VkDescriptorPool create_pool(uint32_t max_sets, const std::vector<VkDescriptorPoolSize>& sizes)
{
    VkDescriptorPoolCreateInfo pool_create_info = {};
    pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create_info.maxSets = max_sets;
    pool_create_info.poolSizeCount = static_cast<uint32_t>(sizes.size());
    pool_create_info.pPoolSizes = sizes.data();

    VkDescriptorPool pool = VK_NULL_HANDLE;

    const VkResult result = device::get_disp().createDescriptorPool(&pool_create_info, nullptr, &pool);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create descriptor pool");

    return pool;
}

void destroy_pool(VkDescriptorPool pool)
{
    device::get_disp().destroyDescriptorPool(pool, nullptr);
}

VkDescriptorSetLayout create_set_layout(const std::vector<VkDescriptorSetLayoutBinding>& bindings)
{
    VkDescriptorSetLayoutCreateInfo layout_create_info = {};
    layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_create_info.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_create_info.pBindings = bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;

    const VkResult result = device::get_disp().createDescriptorSetLayout(&layout_create_info, nullptr, &layout);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create descriptor set layout");

    return layout;
}

void destroy_set_layout(VkDescriptorSetLayout layout)
{
    device::get_disp().destroyDescriptorSetLayout(layout, nullptr);
}

VkDescriptorSet allocate_set(VkDescriptorPool pool, VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;

    const VkResult result = device::get_disp().allocateDescriptorSets(&alloc_info, &set);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to allocate descriptor set");

    return set;
}

void write_image(
    VkDescriptorSet set,
    uint32_t binding,
    VkDescriptorType type,
    VkImageView view,
    VkImageLayout layout,
    VkSampler sampler
)
{
    VkDescriptorImageInfo image_info = {};
    image_info.sampler = sampler;
    image_info.imageView = view;
    image_info.imageLayout = layout;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pImageInfo = &image_info;

    device::get_disp().updateDescriptorSets(1, &write, 0, nullptr);
}

void write_buffer(
    VkDescriptorSet set,
    uint32_t binding,
    VkDescriptorType type,
    const buffer& target,
    VkDeviceSize offset,
    VkDeviceSize range
)
{
    VkDescriptorBufferInfo buffer_info = {};
    buffer_info.buffer = target.handle;
    buffer_info.offset = offset;
    buffer_info.range = range;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pBufferInfo = &buffer_info;

    device::get_disp().updateDescriptorSets(1, &write, 0, nullptr);
}

}
