#include "glp/glaze/rendering/scene_render/taa.h"

#include "glp/glaze/rendering/scene_render/frame_pipeline.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/gpu_camera.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_post.h"
#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec2.hpp>

#include <cstdint>

namespace glaze::rendering::scene_render::taa
{

namespace
{

struct taa_push
{
    glm::mat4 sky_reprojection;
    glm::vec2 screen_size;
    float history_weight;
    uint32_t reset;
};

constexpr uint32_t GROUP_SIZE = 8;

VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;
VkDescriptorPool m_pool = VK_NULL_HANDLE;
VkDescriptorSet m_set = VK_NULL_HANDLE;

VkSampler m_linear_sampler  = VK_NULL_HANDLE;
VkSampler m_nearest_sampler = VK_NULL_HANDLE;

VkImageView m_written_view = VK_NULL_HANDLE;

const shader* m_shader = nullptr;

bool m_history_valid = false;

glm::mat4 m_previous_view_proj = glm::mat4(1.0f);
bool m_has_previous_view_proj  = false;

VkSampler make_sampler(VkFilter filter)
{
    sampler_create_info sampler_info;
    sampler_info.mag_filter = filter;
    sampler_info.min_filter = filter;
    sampler_info.mip_mode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.wrap_u     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_v     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_w     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropy = false;

    return vk::samplers::get(vk::samplers::make(sampler_info));
}

}

void init_all()
{
    m_set_layout = vk::descriptors::create_set_layout({
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    });

    m_pool = vk::descriptors::create_pool(
        1,
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1 }
        }
    );

    m_set = vk::descriptors::allocate_set(m_pool, m_set_layout);

    m_linear_sampler  = make_sampler(VK_FILTER_LINEAR);
    m_nearest_sampler = make_sampler(VK_FILTER_NEAREST);

    compute_pipeline_create_info info;
    info.set_layouts        = { m_set_layout };
    info.push_constant_size = sizeof(taa_push);

    m_shader = res::shaders::build("taa", "", "", "sys/post/taa.comp", {}, info, render_queue::opaque);
}

void clean_all()
{
    vk::descriptors::destroy_pool(m_pool);
    vk::descriptors::destroy_set_layout(m_set_layout);

    m_set_layout = VK_NULL_HANDLE;
    m_pool       = VK_NULL_HANDLE;
    m_set        = VK_NULL_HANDLE;

    m_linear_sampler  = VK_NULL_HANDLE;
    m_nearest_sampler = VK_NULL_HANDLE;

    m_written_view = VK_NULL_HANDLE;

    m_shader = nullptr;

    m_history_valid = false;

    m_has_previous_view_proj = false;
}

void handle_all()
{
    const image& output = it::get_taa_output_image();

    if (output.view == m_written_view) {
        return;
    }

    vk::descriptors::write_image(m_set, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, it::get_hdr_image().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_nearest_sampler);
    vk::descriptors::write_image(m_set, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, it::get_taa_history_image().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_linear_sampler);
    vk::descriptors::write_image(m_set, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, it::get_velocity_image().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_nearest_sampler);
    vk::descriptors::write_image(m_set, 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, output.view, VK_IMAGE_LAYOUT_GENERAL);
    vk::descriptors::write_image(m_set, 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, it::get_depth_image().view, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, m_nearest_sampler);

    m_written_view  = output.view;
    m_history_valid = false;
}

void dispatch()
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const image& hdr      = it::get_hdr_image();
    const image& velocity = it::get_velocity_image();
    const image& history  = it::get_taa_history_image();
    const image& output   = it::get_taa_output_image();
    const image& depth    = it::get_depth_image();

    vk::commands::transition_image(
        cmd,
        depth.handle,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    vk::commands::transition_image(
        cmd,
        hdr.handle,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );

    vk::commands::transition_image(
        cmd,
        velocity.handle,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );

    vk::commands::transition_image(
        cmd,
        history.handle,
        m_history_valid ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );

    vk::commands::transition_image(
        cmd,
        output.handle,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        0,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    const gpu_post& post = frame_pipe::get_post();

    const glm::mat4 view_proj = frame_pipe::has_camera() ? frame_pipe::get_camera().view_proj : glm::mat4(1.0f);

    taa_push push;
    push.sky_reprojection = (m_has_previous_view_proj ? m_previous_view_proj : view_proj) * glm::inverse(view_proj);
    push.screen_size      = glm::vec2(static_cast<float>(output.extent.width), static_cast<float>(output.extent.height));

    m_previous_view_proj     = view_proj;
    m_has_previous_view_proj = true;
    push.history_weight = post.taa_enabled ? post.taa_history_weight : 0.0f;
    push.reset          = m_history_valid ? 0 : 1;

    disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_shader->compute.layout, 0, 1, &m_set, 0, nullptr);

    vk::commands::dispatch(
        cmd,
        m_shader->compute,
        &push,
        sizeof(push),
        (output.extent.width + GROUP_SIZE - 1) / GROUP_SIZE,
        (output.extent.height + GROUP_SIZE - 1) / GROUP_SIZE,
        1
    );

    vk::commands::transition_image(
        cmd,
        output.handle,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT
    );

    vk::commands::transition_image(
        cmd,
        history.handle,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        0,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT
    );

    VkImageCopy region = {};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1;
    region.extent = output.extent;

    disp.cmdCopyImage(
        cmd,
        output.handle,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        history.handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    vk::commands::transition_image(
        cmd,
        output.handle,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        0,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );

    m_history_valid = true;
}

}
