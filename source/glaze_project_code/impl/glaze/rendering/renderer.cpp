#include "glp/glaze/rendering/renderer.h"

#include "glp/glaze/rendering/defines.h"
#include "glp/glaze/rendering/types.h"

#include "glp/glaze/rendering/queue_deletor.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/resources.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/io/viewport.h"
#include "glp/glaze/utilities/core/assert.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_vulkan.h>

#include <vulkan/vulkan_core.h>
#include <vkb/VkBootstrap.h>

#include <cstdint>

namespace glaze::rendering::renderer
{

namespace
{

vkb::Instance m_instance;

pipeline m_pipeline;

void create_instance()
{
    Uint32 sdl_ext_count = 0;
    const char* const* sdl_exts = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);

    GLAZE_ASSERT(sdl_exts == nullptr, SDL_GetError());

    vkb::InstanceBuilder instance_builder;

    auto instance_ret = instance_builder
        .set_app_name("glaze")
        .enable_extensions(sdl_ext_count, sdl_exts)
        .use_default_debug_messenger()
        .request_validation_layers()
        .require_api_version(1, 3, 0)
        .build();

    GLAZE_ASSERT(!instance_ret, instance_ret.error().message());

    m_instance = instance_ret.value();

    queue_deletor::push([]() { vkb::destroy_instance(m_instance); });
}

void create_graphics_pipeline()
{
    graphics_pipeline_info info;
    info.vert_file = "triangle.vert";
    info.frag_file = "triangle.frag";
    info.color_format = vk::frame::get_swapchain().image_format;

    m_pipeline = vk::shaders::create_graphics_pipeline(info);

    queue_deletor::push([]() { vk::shaders::destroy_pipeline(m_pipeline); });
}

void record_command_buffer()
{
    frame_data& frame = vk::frame::get_current_frame();
    swapchain_image_data& image_data = vk::frame::get_current_image();

    const vkb::DispatchTable& disp = vk::device::get_disp();
    const VkExtent2D extent = vk::frame::get_swapchain().extent;

    VkCommandBuffer cmd = frame.command_buffer;

    vk::commands::reset(cmd);
    vk::commands::begin(cmd);

    vk::commands::transition_image(
        cmd,
        image_data.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
    );

    VkRenderingAttachmentInfo color_attachment = {};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = image_data.image_view;
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

    VkRenderingInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset = { 0, 0 };
    rendering_info.renderArea.extent = extent;
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;

    disp.cmdBeginRendering(cmd, &rendering_info);

    disp.cmdSetViewport(cmd, 0, 1, &viewport);
    disp.cmdSetScissor(cmd, 0, 1, &scissor);

    disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.handle);

    disp.cmdDraw(cmd, 3, 1, 0, 0);

    disp.cmdEndRendering(cmd);

    vk::commands::transition_image(
        cmd,
        image_data.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_NONE,
        0
    );

    vk::commands::end(cmd);
}

void draw_frame()
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    frame_data& frame = vk::frame::get_current_frame();

    disp.waitForFences(1, &frame.fence_inflight, VK_TRUE, UINT64_MAX);

    VkResult result = disp.acquireNextImageKHR(
        vk::frame::get_swapchain(),
        UINT64_MAX,
        frame.semaphore_available,
        VK_NULL_HANDLE,
        &vk::frame::get_image_index()
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        vk::frame::recreate_swapchain();
        return;
    }

    GLAZE_ASSERT(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR, "failed to acquire swapchain image");

    disp.resetFences(1, &frame.fence_inflight);

    swapchain_image_data& image_data = vk::frame::get_current_image();

    record_command_buffer();

    VkSemaphoreSubmitInfo wait_info = {};
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait_info.semaphore = frame.semaphore_available;
    wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signal_info = {};
    signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_info.semaphore = image_data.semaphore_finished;
    signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    vk::commands::submit(frame.command_buffer, &wait_info, &signal_info, frame.fence_inflight);

    VkSwapchainKHR swapchain = vk::frame::get_swapchain();

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &image_data.semaphore_finished;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &vk::frame::get_image_index();

    vk::frame::advance();

    result = disp.queuePresentKHR(vk::device::get_queues().present, &present_info);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        vk::frame::recreate_swapchain();
        return;
    }

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to present swapchain image");
}

}

void init()
{
    init_vk();

    vk::commands::create_pool();
    vk::resources::create_allocator(m_instance);

    vk::frame::create_frame_data();

    create_graphics_pipeline();
}

void init_vk()
{
    create_instance();

    vk::device::create_surface(m_instance);
    vk::device::create_device(m_instance);
    vk::device::make_queues();

    vk::frame::create_swapchain();
    queue_deletor::push([]() { vk::frame::destroy_swapchain(); });
}

void cleanup()
{
    vk::device::get_disp().deviceWaitIdle();

    queue_deletor::clean_all();

    m_pipeline = {};
    m_instance = {};
}

void next_frame()
{
    if (io::viewport::get_unhandle_resized()) {
        vk::frame::recreate_swapchain();
    }

    draw_frame();
}

}
