#include "glp/glaze/rendering/scene_render/outline_mask.h"

#include "glp/glaze/rendering/scene_render/frame_pipeline.h"
#include "glp/glaze/rendering/scene_render/picking.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/gpu_model.h"
#include "glp/glaze/rendering/scene_render/resources/meshes.h"
#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <vector>

namespace glaze::rendering::scene_render::outline_mask
{

namespace
{

constexpr VkFormat MASK_FORMAT = VK_FORMAT_R8_UNORM;

image m_mask;
VkExtent2D m_mask_extent = {};

const shader* m_shader = nullptr;

uint32_t m_target_entity = 0;

void create_mask(VkExtent2D extent)
{
    m_mask = vk::resources::create_image(
        { extent.width, extent.height, 1 },
        MASK_FORMAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    m_mask_extent = extent;
}

void destroy_mask()
{
    if (m_mask.handle == VK_NULL_HANDLE) {
        return;
    }

    vk::resources::destroy_image(m_mask);

    m_mask        = {};
    m_mask_extent = {};
}

void create_shader()
{
    graphics_pipeline_create_info info;
    info.color_formats        = { MASK_FORMAT };
    info.cull_mode            = VK_CULL_MODE_NONE;
    info.depth_write          = false;
    info.set_layouts          = { it::get_scene_set_layout() };
    info.push_constant_size   = sizeof(VkDeviceAddress);
    info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    m_shader = res::shaders::build("outline_mask", "sys/post/outline_mask.vert", "sys/post/outline_mask.frag", "", info, {}, render_queue::opaque);
}

}

void init_all()
{
    create_mask(it::get_render_extent());
    create_shader();
}

void clean_all()
{
    destroy_mask();

    m_shader        = nullptr;
    m_target_entity = 0;
}

void handle_all()
{
    const VkExtent2D current = it::get_render_extent();

    if (current.width != m_mask_extent.width || current.height != m_mask_extent.height) {
        vk::device::get_disp().deviceWaitIdle();

        destroy_mask();
        create_mask(current);
    }
}

void dispatch()
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    vk::commands::transition_image(
        cmd,
        m_mask.handle,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
    );

    VkRenderingAttachmentInfo color_attachment = {};
    color_attachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView   = m_mask.view;
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
    color_attachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

    VkRenderingInfo rendering_info = {};
    rendering_info.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset    = { 0, 0 };
    rendering_info.renderArea.extent    = m_mask_extent;
    rendering_info.layerCount           = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments    = &color_attachment;

    disp.cmdBeginRendering(cmd, &rendering_info);

    if (m_target_entity != 0 && frame_pipe::has_camera()) {
        VkViewport viewport = {};
        viewport.x        = 0.0f;
        viewport.y        = 0.0f;
        viewport.width    = static_cast<float>(m_mask_extent.width);
        viewport.height   = static_cast<float>(m_mask_extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = { 0, 0 };
        scissor.extent = m_mask_extent;

        disp.cmdSetViewport(cmd, 0, 1, &viewport);
        disp.cmdSetScissor(cmd, 0, 1, &scissor);

        disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shader->graphics.handle);

        const VkDeviceAddress push = it::get_frame_data_address();

        vk::commands::push_constants(
            cmd,
            m_shader->graphics.layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            sizeof(push),
            &push
        );

        const VkDescriptorSet scene_set = it::get_scene_set();

        disp.cmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_shader->graphics.layout,
            0,
            1,
            &scene_set,
            0,
            nullptr
        );

        const std::vector<gpu_model_part>& objects = frame_pipe::get_objects();

        const mesh* bound_mesh = nullptr;

        for (uint32_t i = 0; i < objects.size(); i++) {
            const gpu_model_part& part = objects[i];

            if ((part.entity_id & picking::ENTITY_ID_MASK) != m_target_entity) {
                continue;
            }

            if (part.target_mesh != bound_mesh) {
                bound_mesh = part.target_mesh;

                disp.cmdBindIndexBuffer(cmd, bound_mesh->index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
            }

            disp.cmdDrawIndexed(cmd, bound_mesh->index_count, 1, 0, 0, i);
        }
    }

    disp.cmdEndRendering(cmd);

    vk::commands::transition_image(
        cmd,
        m_mask.handle,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );
}

void set_entity(uint32_t entity_id)
{
    m_target_entity = entity_id & static_cast<uint32_t>(picking::ENTITY_ID_MASK);
}

const image& get_mask_image()
{
    return m_mask;
}

}
