#include "glp/glaze/rendering/scene_renderer.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/resources.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/utilities/core/timer.h"

#include <glm/vec4.hpp>

namespace glaze::rendering::scene_renderer
{

namespace
{

struct vertex
{
    glm::vec4 position;
    glm::vec4 color;
};

struct move_push
{
    float step;
    uint32_t seed;
};

constexpr float MOVE_SPEED = 0.5f;

pipeline m_triangle_pipeline;
pipeline m_move_pipeline;

buffer m_vertex_buffer;

VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;
VkDescriptorSetLayout m_vertex_set_layout = VK_NULL_HANDLE;
VkDescriptorSet m_vertex_set = VK_NULL_HANDLE;

uint32_t m_frame = 0;

void create_vertex_resources()
{
    const vertex vertices[3] = {
        { {  0.0f, -0.5f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { {  0.5f,  0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { -0.5f,  0.5f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
    };

    m_vertex_buffer = vk::resources::create_buffer(
        sizeof(vertices),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    vk::resources::upload_buffer(m_vertex_buffer, vertices, sizeof(vertices));

    m_descriptor_pool = vk::descriptors::create_pool(1, { { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 } });

    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT;

    m_vertex_set_layout = vk::descriptors::create_set_layout({ binding });

    m_vertex_set = vk::descriptors::allocate_set(m_descriptor_pool, m_vertex_set_layout);

    vk::descriptors::write_buffer(m_vertex_set, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_vertex_buffer);
}

void create_pipelines()
{
    compute_pipeline_info move_info;
    move_info.comp_file = "moving_triangle_vertices.comp";
    move_info.set_layouts = { m_vertex_set_layout };
    move_info.push_constant_size = sizeof(move_push);

    m_move_pipeline = vk::shaders::create_compute_pipeline(move_info);

    graphics_pipeline_info triangle_info;
    triangle_info.vert_file = "triangle.vert";
    triangle_info.frag_file = "triangle.frag";
    triangle_info.color_format = vk::frame::get_swapchain().image_format;
    triangle_info.cull_mode = VK_CULL_MODE_NONE;
    triangle_info.set_layouts = { m_vertex_set_layout };

    m_triangle_pipeline = vk::shaders::create_graphics_pipeline(triangle_info);
}

void move_vertices(VkCommandBuffer cmd)
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_move_pipeline.handle);

    disp.cmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_move_pipeline.layout,
        0,
        1,
        &m_vertex_set,
        0,
        nullptr
    );

    const move_push push = { MOVE_SPEED * utilities::core::timer::delta, m_frame++ };

    vk::commands::push_constants(cmd, m_move_pipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT, sizeof(push), &push);

    disp.cmdDispatch(cmd, 1, 1, 1);

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT
    );
}

void draw_triangle(VkCommandBuffer cmd, const swapchain_image_data& target, VkExtent2D extent)
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

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

    disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_triangle_pipeline.handle);

    disp.cmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_triangle_pipeline.layout,
        0,
        1,
        &m_vertex_set,
        0,
        nullptr
    );

    disp.cmdDraw(cmd, 3, 1, 0, 0);

    disp.cmdEndRendering(cmd);
}

}

void init()
{
    create_vertex_resources();
    create_pipelines();
}

void cleanup()
{
    vk::shaders::destroy_pipeline(m_triangle_pipeline);
    vk::shaders::destroy_pipeline(m_move_pipeline);

    vk::descriptors::destroy_set_layout(m_vertex_set_layout);
    vk::descriptors::destroy_pool(m_descriptor_pool);

    vk::resources::destroy_buffer(m_vertex_buffer);

    m_triangle_pipeline = {};
    m_move_pipeline = {};

    m_descriptor_pool = VK_NULL_HANDLE;
    m_vertex_set_layout = VK_NULL_HANDLE;
    m_vertex_set = VK_NULL_HANDLE;

    m_frame = 0;
}

void draw_all(VkCommandBuffer cmd, const swapchain_image_data& target, VkExtent2D extent)
{
    move_vertices(cmd);
    draw_triangle(cmd, target, extent);
}

}
