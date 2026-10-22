#include "glp/glaze/rendering/scene_render/post_process.h"

#include "glp/glaze/rendering/scene_render/bloom.h"
#include "glp/glaze/rendering/scene_render/exposure.h"
#include "glp/glaze/rendering/scene_render/frame_pipeline.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/gpu_post.h"
#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace glaze::rendering::scene_render::post_process
{

namespace
{

struct tonemap_push
{
    uint32_t tonemapper;
    float sharpen;
    float bloom_intensity;
    float bloom_normalization;
    float inverse_exposure;
    float night_vision;
    float contrast;
    float saturation;
};

VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;
VkDescriptorPool m_pool = VK_NULL_HANDLE;
VkDescriptorSet m_set = VK_NULL_HANDLE;

VkSampler m_sampler = VK_NULL_HANDLE;
VkSampler m_bloom_sampler = VK_NULL_HANDLE;

VkImageView m_written_view = VK_NULL_HANDLE;
VkImageView m_written_bloom_view = VK_NULL_HANDLE;

const shader* m_tonemap_shader = nullptr;

void create_set()
{
    m_set_layout = vk::descriptors::create_set_layout({
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
    });

    m_pool = vk::descriptors::create_pool(1, { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 } });
    m_set  = vk::descriptors::allocate_set(m_pool, m_set_layout);
}

void create_sampler()
{
    sampler_create_info sampler_info;
    sampler_info.mag_filter = VK_FILTER_NEAREST;
    sampler_info.min_filter = VK_FILTER_NEAREST;
    sampler_info.mip_mode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.wrap_u     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_v     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_w     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropy = false;

    m_sampler = vk::samplers::get(vk::samplers::make(sampler_info));

    sampler_info.mag_filter = VK_FILTER_LINEAR;
    sampler_info.min_filter = VK_FILTER_LINEAR;

    m_bloom_sampler = vk::samplers::get(vk::samplers::make(sampler_info));
}

void create_tonemap_shader()
{
    graphics_pipeline_create_info info;
    info.color_formats        = { vk::frame::get_swapchain().image_format };
    info.cull_mode            = VK_CULL_MODE_NONE;
    info.depth_write          = false;
    info.set_layouts          = { m_set_layout };
    info.push_constant_size   = sizeof(tonemap_push);
    info.push_constant_stages = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_tonemap_shader = res::shaders::build("tonemap", "sys/post/tonemap.vert", "sys/post/tonemap.frag", "", info, {}, render_queue::opaque);
}

}

void init_all()
{
    create_set();

    create_sampler();

    create_tonemap_shader();
}

void clean_all()
{
    vk::descriptors::destroy_pool(m_pool);
    vk::descriptors::destroy_set_layout(m_set_layout);

    m_set_layout = VK_NULL_HANDLE;
    m_pool       = VK_NULL_HANDLE;
    m_set        = VK_NULL_HANDLE;

    m_sampler       = VK_NULL_HANDLE;
    m_bloom_sampler = VK_NULL_HANDLE;

    m_written_view       = VK_NULL_HANDLE;
    m_written_bloom_view = VK_NULL_HANDLE;

    m_tonemap_shader = nullptr;
}

void handle_all()
{
    const VkImageView hdr_view   = it::get_output_image().view;
    const VkImageView bloom_view = bloom::get_output_view();

    if (hdr_view != m_written_view) {
        vk::descriptors::write_image(m_set, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, hdr_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_sampler);

        m_written_view = hdr_view;
    }

    if (bloom_view != m_written_bloom_view) {
        vk::descriptors::write_image(m_set, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, bloom_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_bloom_sampler);

        m_written_bloom_view = bloom_view;
    }
}

void draw_all()
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const gpu_post& post = frame_pipe::get_post();

    tonemap_push push;
    push.tonemapper          = post.tonemapper;
    push.sharpen             = post.sharpen;
    push.bloom_intensity     = post.bloom_intensity;
    push.bloom_normalization = 1.0f / static_cast<float>(bloom::get_mip_count());
    push.inverse_exposure    = 1.0f / exposure::get_exposure();
    push.night_vision        = post.night_vision_intensity;
    push.contrast            = post.tonemap_contrast;
    push.saturation          = post.tonemap_saturation;

    disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemap_shader->graphics.handle);

    disp.cmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_tonemap_shader->graphics.layout,
        0,
        1,
        &m_set,
        0,
        nullptr
    );

    vk::commands::push_constants(cmd, m_tonemap_shader->graphics.layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(push), &push);

    disp.cmdDraw(cmd, 3, 1, 0, 0);
}

}
