#include "glp/glaze/rendering/renderer.h"

#include "glp/glaze/rendering/queue_deletor.h"

#include "glp/glaze/rendering/scene_render/atmosphere.h"
#include "glp/glaze/rendering/scene_render/bloom.h"
#include "glp/glaze/rendering/scene_render/clouds.h"
#include "glp/glaze/rendering/scene_render/exposure.h"
#include "glp/glaze/rendering/scene_render/light_shafts.h"
#include "glp/glaze/rendering/scene_render/gtao.h"
#include "glp/glaze/rendering/scene_render/post_process.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"
#include "glp/glaze/rendering/scene_render/taa.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/profiler.h"
#include "glp/glaze/rendering/vk/resources.h"

#include "glp/glaze/io/viewport.h"
#include "glp/glaze/utilities/core/assert.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_vulkan.h>

#include <vulkan/vulkan_core.h>
#include <vkb/VkBootstrap.h>

#include <cstdint>

namespace glaze::rendering::it
{

namespace
{

vkb::Instance m_vk_instance;

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

    m_vk_instance = instance_ret.value();

    queue_deletor::push([]() { vkb::destroy_instance(m_vk_instance); });
}

void set_viewport(VkCommandBuffer cmd, VkExtent2D extent)
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

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

    disp.cmdSetViewport(cmd, 0, 1, &viewport);
    disp.cmdSetScissor(cmd, 0, 1, &scissor);
}

VkRenderingAttachmentInfo make_color_attachment(VkImageView view, VkAttachmentLoadOp load_op)
{
    VkRenderingAttachmentInfo attachment = {};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.resolveMode = VK_RESOLVE_MODE_NONE;
    attachment.loadOp = load_op;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

    return attachment;
}

void transition_color_target(VkCommandBuffer cmd, const image& target)
{
    vk::commands::transition_image(
        cmd,
        target.handle,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
    );
}

void begin_prepass_rendering()
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const image& depth    = scene_render::it::get_depth_image();
    const image& normal   = scene_render::it::get_normal_image();
    const image& velocity = scene_render::it::get_velocity_image();

    const VkExtent2D extent = vk::frame::get_swapchain().extent;

    transition_color_target(cmd, normal);
    transition_color_target(cmd, velocity);

    vk::commands::transition_image(
        cmd,
        depth.handle,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        0,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    const VkRenderingAttachmentInfo color_attachments[2] = {
        make_color_attachment(normal.view, VK_ATTACHMENT_LOAD_OP_CLEAR),
        make_color_attachment(velocity.view, VK_ATTACHMENT_LOAD_OP_CLEAR)
    };

    VkRenderingAttachmentInfo depth_attachment = {};
    depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_attachment.imageView = depth.view;
    depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment.clearValue.depthStencil = { 0.0f, 0 };

    VkRenderingInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset = { 0, 0 };
    rendering_info.renderArea.extent = extent;
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 2;
    rendering_info.pColorAttachments = color_attachments;
    rendering_info.pDepthAttachment = &depth_attachment;

    disp.cmdBeginRendering(cmd, &rendering_info);

    set_viewport(cmd, extent);
}

void begin_scene_rendering()
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const image& hdr   = scene_render::it::get_hdr_image();
    const image& depth = scene_render::it::get_depth_image();

    const VkExtent2D extent = vk::frame::get_swapchain().extent;

    transition_color_target(cmd, hdr);

    vk::commands::transition_image(
        cmd,
        depth.handle,
        VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        0,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    const VkRenderingAttachmentInfo color_attachment = make_color_attachment(hdr.view, VK_ATTACHMENT_LOAD_OP_CLEAR);

    VkRenderingAttachmentInfo depth_attachment = {};
    depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_attachment.imageView = depth.view;
    depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset = { 0, 0 };
    rendering_info.renderArea.extent = extent;
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;
    rendering_info.pDepthAttachment = &depth_attachment;

    disp.cmdBeginRendering(cmd, &rendering_info);

    set_viewport(cmd, extent);
}

void begin_post_rendering()
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const swapchain_image_data& target = vk::frame::get_current_image();

    const VkExtent2D extent = vk::frame::get_swapchain().extent;

    vk::commands::transition_image(
        cmd,
        target.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
    );

    VkRenderingAttachmentInfo color_attachment = {};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = target.image_view;
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset = { 0, 0 };
    rendering_info.renderArea.extent = extent;
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;

    disp.cmdBeginRendering(cmd, &rendering_info);

    set_viewport(cmd, extent);
}

void record_command_buffer()
{
    frame_data& frame = vk::frame::get_current_frame();
    swapchain_image_data& image_data = vk::frame::get_current_image();

    VkCommandBuffer cmd = frame.command_buffer;

    vk::commands::reset(cmd);
    vk::commands::begin(cmd);

    vk::profiler::begin_frame(cmd);

    vk::resources::destroy_used_staging();
    vk::resources::flush_uploads();

    scene_render::it::handle_all();

    vk::profiler::mark(cmd, "uploads");

    scene_render::atmosphere::dispatch();

    vk::profiler::mark(cmd, "atmosphere");

    scene_render::clouds::dispatch();

    vk::profiler::mark(cmd, "clouds");

    begin_prepass_rendering();

    scene_render::it::draw_prepass();

    vk::device::get_disp().cmdEndRendering(cmd);

    vk::profiler::mark(cmd, "prepass");

    scene_render::gtao::dispatch();

    vk::profiler::mark(cmd, "gtao");

    begin_scene_rendering();

    scene_render::it::draw_all();

    vk::device::get_disp().cmdEndRendering(cmd);

    vk::profiler::mark(cmd, "scene");

    scene_render::taa::dispatch();

    vk::profiler::mark(cmd, "taa");

    scene_render::light_shafts::dispatch();

    vk::profiler::mark(cmd, "shafts");

    scene_render::exposure::dispatch();

    vk::profiler::mark(cmd, "exposure");

    scene_render::bloom::dispatch();

    vk::profiler::mark(cmd, "bloom");

    begin_post_rendering();

    scene_render::post_process::draw_all();

    vk::device::get_disp().cmdEndRendering(cmd);

    vk::profiler::mark(cmd, "post");

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
    vk::resources::create_allocator(m_vk_instance);

    vk::frame::create_frame_data();

    vk::profiler::create();
    queue_deletor::push([]() { vk::profiler::destroy(); });

    scene_render::it::init();
}

void init_vk()
{
    create_instance();

    vk::device::create_surface(m_vk_instance);
    vk::device::create_device(m_vk_instance);
    vk::device::make_queues();

    vk::frame::create_swapchain();
    queue_deletor::push([]() { vk::frame::destroy_swapchain(); });
}

void cleanup()
{
    vk::device::get_disp().deviceWaitIdle();

    scene_render::it::cleanup();

    queue_deletor::clean_all();

    m_vk_instance = {};
}

void render_frame()
{
    const glm::ivec2 extent = io::viewport::get_extent();

    if (extent.x == 0 || extent.y == 0) {
        return;
    }

    if (io::viewport::get_unhandle_resized()) {
        vk::frame::recreate_swapchain();
    }

    draw_frame();
}

}
