#include "glp/glaze/rendering/vk/device.h"

#include "glp/glaze/rendering/queue_deletor.h"

#include "glp/glaze/io/viewport.h"
#include "glp/glaze/utilities/core/assert.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_vulkan.h>

#include <string>

namespace glaze::rendering::vk::device
{

namespace
{

VkSurfaceKHR m_surface = VK_NULL_HANDLE;

vkb::PhysicalDevice m_physical;
vkb::Device m_device;
vkb::DispatchTable m_disp;

queue_set m_queues;

}

void create_surface(const vkb::Instance& instance)
{
    const bool created = SDL_Vulkan_CreateSurface(io::viewport::get_native(), instance, nullptr, &m_surface);

    GLAZE_ASSERT(!created, SDL_GetError());

    queue_deletor::push([instance, surface = m_surface]() { vkb::destroy_surface(instance, surface); });
}

void create_device(const vkb::Instance& instance)
{
    VkPhysicalDeviceVulkan13Features features13 = {};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    features13.shaderDemoteToHelperInvocation = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12 = {};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.descriptorIndexing = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    features12.drawIndirectCount = VK_TRUE;

    VkPhysicalDeviceFeatures features = {};
    features.samplerAnisotropy = VK_TRUE;
    features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    features.depthClamp = VK_TRUE;
    features.shaderClipDistance = VK_TRUE;
    features.multiDrawIndirect = VK_TRUE;
    features.drawIndirectFirstInstance = VK_TRUE;

    vkb::PhysicalDeviceSelector phys_device_selector(instance);

    auto phys_device_ret = phys_device_selector
        .set_surface(m_surface)
        .set_minimum_version(1, 3)
        .set_required_features(features)
        .set_required_features_12(features12)
        .set_required_features_13(features13)
        .select();

    if (!phys_device_ret) {
        std::string message = phys_device_ret.error().message();

        for (const std::string& reason : phys_device_ret.detailed_failure_reasons()) {
            message += "\n    " + reason;
        }

        GLAZE_ASSERT(true, message);
    }

    m_physical = phys_device_ret.value();

    vkb::DeviceBuilder device_builder(m_physical);

    auto device_ret = device_builder.build();

    GLAZE_ASSERT(!device_ret, device_ret.error().message());

    m_device = device_ret.value();
    m_disp   = m_device.make_table();

    queue_deletor::push([]() { vkb::destroy_device(m_device); });
}

void make_queues()
{
    auto gq = m_device.get_queue(vkb::QueueType::graphics);

    GLAZE_ASSERT(!gq.has_value(), "failed to get graphics queue: " + gq.error().message());

    m_queues.graphics = gq.value();

    auto pq = m_device.get_queue(vkb::QueueType::present);

    GLAZE_ASSERT(!pq.has_value(), "failed to get present queue: " + pq.error().message());

    m_queues.present = pq.value();

    m_queues.graphics_family = m_device.get_queue_index(vkb::QueueType::graphics).value();
    m_queues.present_family  = m_device.get_queue_index(vkb::QueueType::present).value();
}

const vkb::PhysicalDevice& get_physical() { return m_physical; }
const vkb::Device& get_device()           { return m_device; }
const vkb::DispatchTable& get_disp()      { return m_disp; }

VkSurfaceKHR get_surface() { return m_surface; }

const queue_set& get_queues() { return m_queues; }

}
