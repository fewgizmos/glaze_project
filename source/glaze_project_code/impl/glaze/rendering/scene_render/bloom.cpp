#include "glp/glaze/rendering/scene_render/bloom.h"

#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/resources.h"
#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"

#include <algorithm>
#include <cstdint>

namespace glaze::rendering::scene_render::bloom
{

namespace
{

constexpr uint32_t MAX_MIPS   = 6;
constexpr uint32_t GROUP_SIZE = 8;

constexpr uint32_t SETS_COUNT = MAX_MIPS * 2;

VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;
VkDescriptorPool m_pool = VK_NULL_HANDLE;

VkDescriptorSet m_downsample_sets[MAX_MIPS] = {};
VkDescriptorSet m_upsample_sets[MAX_MIPS] = {};

VkSampler m_sampler = VK_NULL_HANDLE;

image m_chain;
VkImageView m_mip_views[MAX_MIPS] = {};

uint32_t m_mip_count = 0;

VkImageView m_written_view = VK_NULL_HANDLE;

const shader* m_downsample_shader = nullptr;
const shader* m_upsample_shader = nullptr;

uint32_t get_groups(uint32_t size)
{
    return (size + GROUP_SIZE - 1) / GROUP_SIZE;
}

uint32_t mip_size(uint32_t size, uint32_t mip)
{
    return std::max(1U, size >> mip);
}

void destroy_chain()
{
    for (uint32_t mip = 0; mip < m_mip_count; mip++) {
        vk::resources::destroy_image_view(m_mip_views[mip]);

        m_mip_views[mip] = VK_NULL_HANDLE;
    }

    vk::resources::destroy_image(m_chain);

    m_mip_count = 0;
}

void create_chain(const image& source)
{
    const uint32_t width  = std::max(1U, source.extent.width / 2);
    const uint32_t height = std::max(1U, source.extent.height / 2);

    m_mip_count = 1;

    while (m_mip_count < MAX_MIPS && mip_size(std::min(width, height), m_mip_count) > 1) {
        m_mip_count++;
    }

    m_chain = vk::resources::create_image(
        { width, height, 1 },
        HDR_FORMAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        m_mip_count
    );

    for (uint32_t mip = 0; mip < m_mip_count; mip++) {
        m_mip_views[mip] = vk::resources::create_image_view(m_chain, VK_IMAGE_VIEW_TYPE_2D, mip, 1);
    }

    for (uint32_t mip = 0; mip < m_mip_count; mip++) {
        if (mip == 0) {
            vk::descriptors::write_image(m_downsample_sets[mip], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, source.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_sampler);
        } else {
            vk::descriptors::write_image(m_downsample_sets[mip], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_mip_views[mip - 1], VK_IMAGE_LAYOUT_GENERAL, m_sampler);
        }

        vk::descriptors::write_image(m_downsample_sets[mip], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_mip_views[mip], VK_IMAGE_LAYOUT_GENERAL);
    }

    for (uint32_t mip = 0; mip + 1 < m_mip_count; mip++) {
        vk::descriptors::write_image(m_upsample_sets[mip], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_mip_views[mip + 1], VK_IMAGE_LAYOUT_GENERAL, m_sampler);
        vk::descriptors::write_image(m_upsample_sets[mip], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_mip_views[mip], VK_IMAGE_LAYOUT_GENERAL);
    }
}

void record_pass(VkCommandBuffer cmd, const shader* target_shader, VkDescriptorSet set, uint32_t mip)
{
    vk::device::get_disp().cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, target_shader->compute.layout, 0, 1, &set, 0, nullptr);

    vk::commands::dispatch(
        cmd,
        target_shader->compute,
        nullptr,
        0,
        get_groups(mip_size(m_chain.extent.width, mip)),
        get_groups(mip_size(m_chain.extent.height, mip)),
        1
    );

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );
}

}

void init_all()
{
    m_set_layout = vk::descriptors::create_set_layout({
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    });

    m_pool = vk::descriptors::create_pool(
        SETS_COUNT,
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SETS_COUNT },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          SETS_COUNT }
        }
    );

    for (uint32_t mip = 0; mip < MAX_MIPS; mip++) {
        m_downsample_sets[mip] = vk::descriptors::allocate_set(m_pool, m_set_layout);
        m_upsample_sets[mip]   = vk::descriptors::allocate_set(m_pool, m_set_layout);
    }

    sampler_create_info sampler_info;
    sampler_info.mip_mode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.wrap_u     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_v     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_w     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropy = false;

    m_sampler = vk::samplers::get(vk::samplers::make(sampler_info));

    compute_pipeline_create_info info;
    info.set_layouts = { m_set_layout };

    m_downsample_shader = res::shaders::build("bloom_downsample", "", "", "sys/bloom_downsample.comp", {}, info, render_queue::opaque);
    m_upsample_shader   = res::shaders::build("bloom_upsample", "", "", "sys/bloom_upsample.comp", {}, info, render_queue::opaque);
}

void clean_all()
{
    destroy_chain();

    vk::descriptors::destroy_pool(m_pool);
    vk::descriptors::destroy_set_layout(m_set_layout);

    m_set_layout = VK_NULL_HANDLE;
    m_pool       = VK_NULL_HANDLE;

    m_sampler      = VK_NULL_HANDLE;
    m_written_view = VK_NULL_HANDLE;

    m_downsample_shader = nullptr;
    m_upsample_shader   = nullptr;
}

void handle_all()
{
    const image& source = it::get_taa_output_image();

    if (source.view == m_written_view) {
        return;
    }

    if (m_chain.handle != VK_NULL_HANDLE) {
        vk::device::get_disp().deviceWaitIdle();

        destroy_chain();
    }

    create_chain(source);

    m_written_view = source.view;
}

void dispatch()
{
    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    vk::commands::transition_image(
        cmd,
        m_chain.handle,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        0,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        m_mip_count
    );

    for (uint32_t mip = 0; mip < m_mip_count; mip++) {
        record_pass(cmd, m_downsample_shader, m_downsample_sets[mip], mip);
    }

    for (uint32_t mip = m_mip_count - 1; mip > 0; mip--) {
        record_pass(cmd, m_upsample_shader, m_upsample_sets[mip - 1], mip - 1);
    }

    vk::commands::transition_image(
        cmd,
        m_chain.handle,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        m_mip_count
    );
}

VkImageView get_output_view() { return m_mip_views[0]; }

uint32_t get_mip_count() { return m_mip_count; }

}
