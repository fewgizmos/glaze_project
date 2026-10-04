#include "glp/glaze/rendering/vk/frame.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/device.h"

#include "glp/glaze/rendering/defines.h"
#include "glp/glaze/rendering/queue_deletor.h"

#include "glp/glaze/io/viewport.h"
#include "glp/glaze/utilities/core/assert.h"

namespace glaze::rendering::vk::frame
{

namespace
{

struct swapchain_state
{
    vkb::Swapchain handle;
    std::vector<swapchain_image_data> images;
};

swapchain_state m_swapchain;

std::vector<frame_data> m_frames;

size_t m_current_frame = 0;
uint32_t m_image_index = 0;

VkSemaphore make_semaphore()
{
    VkSemaphoreCreateInfo semaphore_create_info = {};
    semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkSemaphore semaphore = VK_NULL_HANDLE;

    const VkResult result = device::get_disp().createSemaphore(&semaphore_create_info, nullptr, &semaphore);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create semaphore");

    return semaphore;
}

VkFence make_fence(VkFenceCreateFlags flags)
{
    VkFenceCreateInfo fence_create_info = {};
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_create_info.flags = flags;

    VkFence fence = VK_NULL_HANDLE;

    const VkResult result = device::get_disp().createFence(&fence_create_info, nullptr, &fence);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create fence");

    return fence;
}

void build_swapchain()
{
    const glm::ivec2 extent = io::viewport::get_extent();

    vkb::SwapchainBuilder swapchain_builder(device::get_device());

    auto swap_ret = swapchain_builder
        .set_old_swapchain(m_swapchain.handle)
        .set_desired_extent(static_cast<uint32_t>(extent.x), static_cast<uint32_t>(extent.y))
        .build();

    GLAZE_ASSERT(!swap_ret, swap_ret.error().message());

    vkb::destroy_swapchain(m_swapchain.handle);

    m_swapchain.handle = swap_ret.value();
}

void create_swapchain_image_data()
{
    auto images      = m_swapchain.handle.get_images();
    auto image_views = m_swapchain.handle.get_image_views();

    GLAZE_ASSERT(!images || !image_views, "failed to get swapchain images");

    m_swapchain.images.resize(m_swapchain.handle.image_count);

    for (uint32_t i = 0; i < m_swapchain.handle.image_count; i++) {
        swapchain_image_data& image_data = m_swapchain.images[i];

        image_data.image      = images.value()[i];
        image_data.image_view = image_views.value()[i];

        image_data.semaphore_finished = make_semaphore();
    }
}

void destroy_swapchain_image_data()
{
    for (swapchain_image_data& image_data : m_swapchain.images) {
        device::get_disp().destroyImageView(image_data.image_view, nullptr);
        device::get_disp().destroySemaphore(image_data.semaphore_finished, nullptr);
    }

    m_swapchain.images.clear();
}

}

void create_swapchain()
{
    build_swapchain();
    create_swapchain_image_data();
}

void destroy_swapchain()
{
    destroy_swapchain_image_data();

    vkb::destroy_swapchain(m_swapchain.handle);
}

void recreate_swapchain()
{
    device::get_disp().deviceWaitIdle();

    destroy_swapchain_image_data();

    create_swapchain();

    io::viewport::clear_unhandle_resized();
}

void create_frame_data()
{
    m_frames.resize(MAX_FRAMES_IN_FLIGHT);

    for (frame_data& frame : m_frames) {
        frame.command_buffer = commands::allocate();

        frame.semaphore_available = make_semaphore();

        queue_deletor::push([semaphore = frame.semaphore_available]() {
            device::get_disp().destroySemaphore(semaphore, nullptr);
        });

        frame.fence_inflight = make_fence(VK_FENCE_CREATE_SIGNALED_BIT);

        queue_deletor::push([fence = frame.fence_inflight]() {
            device::get_disp().destroyFence(fence, nullptr);
        });
    }
}

void advance()
{
    m_current_frame = (m_current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

const vkb::Swapchain& get_swapchain() { return m_swapchain.handle; }

frame_data& get_current_frame()             { return m_frames.at(m_current_frame);         }
swapchain_image_data& get_current_image()   { return m_swapchain.images.at(m_image_index); }

uint32_t& get_image_index() { return m_image_index; }

uint32_t get_current_frame_index() { return static_cast<uint32_t>(m_current_frame); }

}
