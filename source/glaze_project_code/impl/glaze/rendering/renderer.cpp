#include "glp/glaze/rendering/renderer.h"

#include "glp/glaze/rendering/vk_deletor.h"

#include "glp/glaze/io/viewport.h"
#include "glp/glaze/utilities/core/assert.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_vulkan.h>

#include <vulkan/vulkan_core.h>
#include <vkb/VkBootstrap.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace glaze::rendering::renderer
{

namespace
{

constexpr int MAX_FRAMES_IN_FLIGHT = 4;

struct vk_context
{
    vkb::Instance instance;
    vkb::InstanceDispatchTable inst_disp;

    VkSurfaceKHR surface = VK_NULL_HANDLE;

    vkb::Device device;
    vkb::DispatchTable disp;

    vkb::Swapchain swapchain;
};

struct frame_data
{
    VkFence fence_inflight = VK_NULL_HANDLE;
    VkSemaphore semaphore_available = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
};

struct swapchain_image_data
{
    VkSemaphore semaphore_finished = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
};

struct render_state
{
    VkQueue graphics_queue = VK_NULL_HANDLE;
    VkQueue present_queue  = VK_NULL_HANDLE;

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline graphics_pipeline = VK_NULL_HANDLE;

    VkCommandPool command_pool = VK_NULL_HANDLE;
    std::vector<frame_data> frames;

    std::vector<swapchain_image_data> swapchain_images;

    size_t current_frame = 0;
    uint32_t current_swapchain_image = 0;
};

vk_context m_ctx;
render_state m_state;

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

    m_ctx.instance  = instance_ret.value();
    m_ctx.inst_disp = m_ctx.instance.make_table();

    vk_deletor::push([]() { vkb::destroy_instance(m_ctx.instance); });
}

void create_surface()
{
    const bool created = SDL_Vulkan_CreateSurface(io::viewport::get_native(), m_ctx.instance, nullptr, &m_ctx.surface);

    GLAZE_ASSERT(!created, SDL_GetError());

    vk_deletor::push([]() { vkb::destroy_surface(m_ctx.instance, m_ctx.surface); });
}

void create_device()
{
    VkPhysicalDeviceVulkan13Features features13 = {};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    vkb::PhysicalDeviceSelector phys_device_selector(m_ctx.instance);

    auto phys_device_ret = phys_device_selector
        .set_surface(m_ctx.surface)
        .set_minimum_version(1, 3)
        .set_required_features_13(features13)
        .select();

    if (!phys_device_ret) {
        std::string message = phys_device_ret.error().message();

        for (const std::string& reason : phys_device_ret.detailed_failure_reasons()) {
            message += "\n    " + reason;
        }

        GLAZE_ASSERT(true, message);
    }

    vkb::DeviceBuilder device_builder(phys_device_ret.value());

    auto device_ret = device_builder.build();

    GLAZE_ASSERT(!device_ret, device_ret.error().message());

    m_ctx.device = device_ret.value();
    m_ctx.disp   = m_ctx.device.make_table();

    vk_deletor::push([]() { vkb::destroy_device(m_ctx.device); });
}

void create_swapchain()
{
    const glm::ivec2 extent = io::viewport::get_extent();

    vkb::SwapchainBuilder swapchain_builder(m_ctx.device);

    auto swap_ret = swapchain_builder
        .set_old_swapchain(m_ctx.swapchain)
        .set_desired_extent(static_cast<uint32_t>(extent.x), static_cast<uint32_t>(extent.y))
        .build();

    GLAZE_ASSERT(!swap_ret, swap_ret.error().message());

    vkb::destroy_swapchain(m_ctx.swapchain);

    m_ctx.swapchain = swap_ret.value();
}

void get_queues()
{
    auto gq = m_ctx.device.get_queue(vkb::QueueType::graphics);

    GLAZE_ASSERT(!gq.has_value(), "failed to get graphics queue: " + gq.error().message());

    m_state.graphics_queue = gq.value();

    auto pq = m_ctx.device.get_queue(vkb::QueueType::present);

    GLAZE_ASSERT(!pq.has_value(), "failed to get present queue: " + pq.error().message());

    m_state.present_queue = pq.value();
}

std::vector<char> read_file(const std::string& filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    GLAZE_ASSERT(!file.is_open(), "failed to open file: " + filename);

    const size_t file_size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(file_size);

    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(file_size));

    return buffer;
}

VkShaderModule create_shader_module(const std::vector<char>& code)
{
    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = code.size();
    create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shader_module = VK_NULL_HANDLE;

    const VkResult result = m_ctx.disp.createShaderModule(&create_info, nullptr, &shader_module);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create shader module");

    return shader_module;
}

void create_graphics_pipeline()
{
    VkShaderModule vert_module = create_shader_module(read_file("source/glaze_project_assets/shaders/triangle.vert.spv"));
    VkShaderModule frag_module = create_shader_module(read_file("source/glaze_project_assets/shaders/triangle.frag.spv"));

    VkPipelineShaderStageCreateInfo vert_stage_info = {};
    vert_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage_info.module = vert_module;
    vert_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo frag_stage_info = {};
    frag_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage_info.module = frag_module;
    frag_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo shader_stages[] = { vert_stage_info, frag_stage_info };

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 0;
    vertex_input_info.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachment = {};
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 0;
    pipeline_layout_info.pushConstantRangeCount = 0;

    VkResult result = m_ctx.disp.createPipelineLayout(&pipeline_layout_info, nullptr, &m_state.pipeline_layout);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create pipeline layout");

    vk_deletor::push([layout = m_state.pipeline_layout]() { m_ctx.disp.destroyPipelineLayout(layout, nullptr); });

    const VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo dynamic_info = {};
    dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_info.dynamicStateCount = 2;
    dynamic_info.pDynamicStates = dynamic_states;

    const VkFormat color_format = m_ctx.swapchain.image_format;

    VkPipelineRenderingCreateInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &color_format;
    rendering_info.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    rendering_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &rendering_info;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_info;
    pipeline_info.layout = m_state.pipeline_layout;
    pipeline_info.renderPass = VK_NULL_HANDLE;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

    result = m_ctx.disp.createGraphicsPipelines(VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &m_state.graphics_pipeline);

    m_ctx.disp.destroyShaderModule(frag_module, nullptr);
    m_ctx.disp.destroyShaderModule(vert_module, nullptr);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create graphics pipeline");

    vk_deletor::push([pipeline = m_state.graphics_pipeline]() { m_ctx.disp.destroyPipeline(pipeline, nullptr); });
}

void create_command_pool()
{
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = m_ctx.device.get_queue_index(vkb::QueueType::graphics).value();

    const VkResult result = m_ctx.disp.createCommandPool(&pool_info, nullptr, &m_state.command_pool);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create command pool");

    vk_deletor::push([pool = m_state.command_pool]() { m_ctx.disp.destroyCommandPool(pool, nullptr); });
}

void create_frame_data()
{
    m_state.frames.resize(MAX_FRAMES_IN_FLIGHT);

    for (frame_data& frame : m_state.frames) {
        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = m_state.command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;

        VkResult result = m_ctx.disp.allocateCommandBuffers(&alloc_info, &frame.command_buffer);

        GLAZE_ASSERT(result != VK_SUCCESS, "failed to allocate command buffer");

        VkSemaphoreCreateInfo semaphore_info = {};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        result = m_ctx.disp.createSemaphore(&semaphore_info, nullptr, &frame.semaphore_available);

        GLAZE_ASSERT(result != VK_SUCCESS, "failed to create frame semaphore");

        vk_deletor::push([semaphore = frame.semaphore_available]() { m_ctx.disp.destroySemaphore(semaphore, nullptr); });

        result = m_ctx.disp.createFence(&fence_info, nullptr, &frame.fence_inflight);

        GLAZE_ASSERT(result != VK_SUCCESS, "failed to create frame fence");

        vk_deletor::push([fence = frame.fence_inflight]() { m_ctx.disp.destroyFence(fence, nullptr); });
    }
}

void create_swapchain_image_data()
{
    auto images      = m_ctx.swapchain.get_images();
    auto image_views = m_ctx.swapchain.get_image_views();

    GLAZE_ASSERT(!images || !image_views, "failed to get swapchain images");

    m_state.swapchain_images.resize(m_ctx.swapchain.image_count);

    for (uint32_t i = 0; i < m_ctx.swapchain.image_count; i++) {
        swapchain_image_data& image_data = m_state.swapchain_images[i];

        image_data.image      = images.value()[i];
        image_data.image_view = image_views.value()[i];

        VkSemaphoreCreateInfo semaphore_info = {};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        const VkResult result = m_ctx.disp.createSemaphore(&semaphore_info, nullptr, &image_data.semaphore_finished);

        GLAZE_ASSERT(result != VK_SUCCESS, "failed to create swapchain image semaphore");
    }
}

void destroy_swapchain_image_data()
{
    for (swapchain_image_data& image_data : m_state.swapchain_images) {
        m_ctx.disp.destroyImageView(image_data.image_view, nullptr);
        m_ctx.disp.destroySemaphore(image_data.semaphore_finished, nullptr);
    }

    m_state.swapchain_images.clear();
}

void recreate_swapchain()
{
    m_ctx.disp.deviceWaitIdle();

    destroy_swapchain_image_data();

    create_swapchain();
    create_swapchain_image_data();

    io::viewport::clear_unhandle_resized();
}

void transition_image(
    VkCommandBuffer cmd,
    VkImage image,
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
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkDependencyInfo dependency_info = {};
    dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency_info.imageMemoryBarrierCount = 1;
    dependency_info.pImageMemoryBarriers = &barrier;

    m_ctx.disp.cmdPipelineBarrier2(cmd, &dependency_info);
}

void record_command_buffer()
{
    frame_data& frame = m_state.frames.at(m_state.current_frame);
    swapchain_image_data& image_data = m_state.swapchain_images.at(m_state.current_swapchain_image);

    VkCommandBuffer cmd = frame.command_buffer;

    VkResult result = m_ctx.disp.resetCommandBuffer(cmd, 0);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to reset command buffer");

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    result = m_ctx.disp.beginCommandBuffer(cmd, &begin_info);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to begin command buffer");

    transition_image(
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
    rendering_info.renderArea.extent = m_ctx.swapchain.extent;
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_ctx.swapchain.extent.width);
    viewport.height = static_cast<float>(m_ctx.swapchain.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = m_ctx.swapchain.extent;

    m_ctx.disp.cmdBeginRendering(cmd, &rendering_info);

    m_ctx.disp.cmdSetViewport(cmd, 0, 1, &viewport);
    m_ctx.disp.cmdSetScissor(cmd, 0, 1, &scissor);

    m_ctx.disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_state.graphics_pipeline);

    m_ctx.disp.cmdDraw(cmd, 3, 1, 0, 0);

    m_ctx.disp.cmdEndRendering(cmd);

    transition_image(
        cmd,
        image_data.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_NONE,
        0
    );

    result = m_ctx.disp.endCommandBuffer(cmd);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to record command buffer");
}

void draw_frame()
{
    frame_data& frame = m_state.frames.at(m_state.current_frame);

    m_ctx.disp.waitForFences(1, &frame.fence_inflight, VK_TRUE, UINT64_MAX);

    VkResult result = m_ctx.disp.acquireNextImageKHR(
        m_ctx.swapchain,
        UINT64_MAX,
        frame.semaphore_available,
        VK_NULL_HANDLE,
        &m_state.current_swapchain_image
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return;
    }

    GLAZE_ASSERT(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR, "failed to acquire swapchain image");

    m_ctx.disp.resetFences(1, &frame.fence_inflight);

    swapchain_image_data& image_data = m_state.swapchain_images.at(m_state.current_swapchain_image);

    record_command_buffer();

    VkSemaphoreSubmitInfo wait_info = {};
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait_info.semaphore = frame.semaphore_available;
    wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signal_info = {};
    signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_info.semaphore = image_data.semaphore_finished;
    signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo cmd_info = {};
    cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmd_info.commandBuffer = frame.command_buffer;

    VkSubmitInfo2 submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.waitSemaphoreInfoCount = 1;
    submit_info.pWaitSemaphoreInfos = &wait_info;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &cmd_info;
    submit_info.signalSemaphoreInfoCount = 1;
    submit_info.pSignalSemaphoreInfos = &signal_info;

    result = m_ctx.disp.queueSubmit2(m_state.graphics_queue, 1, &submit_info, frame.fence_inflight);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to submit draw command buffer");

    VkSwapchainKHR swapchain = m_ctx.swapchain;

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &image_data.semaphore_finished;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &m_state.current_swapchain_image;

    m_state.current_frame = (m_state.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;

    result = m_ctx.disp.queuePresentKHR(m_state.present_queue, &present_info);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreate_swapchain();
        return;
    }

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to present swapchain image");
}

}

void init()
{
    init_vk();

    create_graphics_pipeline();
    create_command_pool();
    create_frame_data();

    create_swapchain_image_data();
    vk_deletor::push([]() { destroy_swapchain_image_data(); });
}

void init_vk()
{
    create_instance();
    create_surface();
    create_device();

    create_swapchain();
    vk_deletor::push([]() { vkb::destroy_swapchain(m_ctx.swapchain); });

    get_queues();
}

void cleanup()
{
    m_ctx.disp.deviceWaitIdle();

    vk_deletor::clean_all();

    m_state = {};
    m_ctx   = {};
}

void next_frame()
{
    if (io::viewport::get_unhandle_resized()) {
        recreate_swapchain();
    }

    draw_frame();
}

}
