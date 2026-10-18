#include "glp/glaze/rendering/scene_render/gtao.h"

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

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace glaze::rendering::scene_render::gtao
{

namespace
{

struct gtao_push
{
    glm::mat4 inv_proj;

    glm::vec2 screen_size;
    float proj_scale;
    float radius;

    float intensity;
    uint32_t directions;
    uint32_t steps;
    uint32_t frame_index;
};

constexpr uint32_t GROUP_SIZE = 8;
constexpr uint32_t MIN_STEPS  = 2;

VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;
VkDescriptorPool m_pool = VK_NULL_HANDLE;
VkDescriptorSet m_set = VK_NULL_HANDLE;

VkSampler m_sampler = VK_NULL_HANDLE;

VkImageView m_written_view = VK_NULL_HANDLE;

const shader* m_shader = nullptr;
const shader* m_fill_shader = nullptr;

uint32_t m_frame_index = 0;

}

void init_all()
{
    m_set_layout = vk::descriptors::create_set_layout({
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    });

    m_pool = vk::descriptors::create_pool(
        1,
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1 }
        }
    );

    m_set = vk::descriptors::allocate_set(m_pool, m_set_layout);

    sampler_create_info sampler_info;
    sampler_info.mag_filter = VK_FILTER_NEAREST;
    sampler_info.min_filter = VK_FILTER_NEAREST;
    sampler_info.mip_mode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.wrap_u     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_v     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_w     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropy = false;

    m_sampler = vk::samplers::get(vk::samplers::make(sampler_info));

    compute_pipeline_create_info info;
    info.set_layouts        = { m_set_layout };
    info.push_constant_size = sizeof(gtao_push);

    m_shader      = res::shaders::build("gtao", "", "", "sys/frame/gtao.comp", {}, info, render_queue::opaque);
    m_fill_shader = res::shaders::build("gtao_fill", "", "", "sys/frame/gtao_fill.comp", {}, info, render_queue::opaque);
}

void clean_all()
{
    vk::descriptors::destroy_pool(m_pool);
    vk::descriptors::destroy_set_layout(m_set_layout);

    m_set_layout = VK_NULL_HANDLE;
    m_pool       = VK_NULL_HANDLE;
    m_set        = VK_NULL_HANDLE;

    m_sampler      = VK_NULL_HANDLE;
    m_written_view = VK_NULL_HANDLE;

    m_shader      = nullptr;
    m_fill_shader = nullptr;
}

void handle_all()
{
    const image& target = it::get_gtao_image();

    if (target.view == m_written_view) {
        return;
    }

    vk::descriptors::write_image(m_set, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, it::get_depth_image().view, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, m_sampler);
    vk::descriptors::write_image(m_set, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, it::get_normal_image().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_sampler);
    vk::descriptors::write_image(m_set, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, target.view, VK_IMAGE_LAYOUT_GENERAL);

    m_written_view = target.view;
}

void dispatch()
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const image& depth  = it::get_depth_image();
    const image& normal = it::get_normal_image();
    const image& target = it::get_gtao_image();

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
        normal.handle,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );

    vk::commands::transition_image(
        cmd,
        target.handle,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        0,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    const gpu_camera& camera = frame_pipe::get_camera();
    const gpu_post& post     = frame_pipe::get_post();

    gtao_push push;
    push.inv_proj    = glm::inverse(camera.proj);
    push.screen_size = glm::vec2(static_cast<float>(target.extent.width), static_cast<float>(target.extent.height));
    push.proj_scale  = 0.5f * static_cast<float>(target.extent.height) * std::abs(camera.proj[1][1]);
    push.radius      = post.gtao_radius;
    push.intensity   = post.gtao_intensity;
    push.directions  = post.gtao_directions;
    push.steps       = std::max(MIN_STEPS, static_cast<uint32_t>(std::lround(static_cast<float>(post.gtao_steps) * it::get_quality())));
    push.frame_index = m_frame_index++;

    disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_shader->compute.layout, 0, 1, &m_set, 0, nullptr);

    const uint32_t half_width = (target.extent.width + 1) / 2;

    vk::commands::dispatch(
        cmd,
        m_shader->compute,
        &push,
        sizeof(push),
        (half_width + GROUP_SIZE - 1) / GROUP_SIZE,
        (target.extent.height + GROUP_SIZE - 1) / GROUP_SIZE,
        1
    );

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    vk::commands::dispatch(
        cmd,
        m_fill_shader->compute,
        &push,
        sizeof(push),
        (half_width + GROUP_SIZE - 1) / GROUP_SIZE,
        (target.extent.height + GROUP_SIZE - 1) / GROUP_SIZE,
        1
    );

    vk::commands::transition_image(
        cmd,
        target.handle,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );
}

}
