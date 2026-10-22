#include "glp/glaze/rendering/scene_render/light_shafts.h"

#include "glp/glaze/rendering/scene_render/atmosphere.h"
#include "glp/glaze/rendering/scene_render/clouds.h"
#include "glp/glaze/rendering/scene_render/exposure.h"
#include "glp/glaze/rendering/scene_render/frame_pipeline.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/gpu_atmosphere.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_camera.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_post.h"
#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/resources.h"
#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"

#include <glm/common.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cstdint>

namespace glaze::rendering::scene_render::light_shafts
{

namespace
{

struct shafts_push
{
    glm::vec4 light_direction;
    glm::vec4 light_color;
    VkDeviceAddress params;
    glm::vec2 sun_uv;
    float intensity;
    float aspect;
    float glare;
    float padding0;
};

constexpr uint32_t DEPTH_BINDING         = 0;
constexpr uint32_t CLOUDS_BINDING        = 1;
constexpr uint32_t TRANSMITTANCE_BINDING = 2;
constexpr uint32_t SHAFTS_BINDING        = 3;
constexpr uint32_t TARGET_BINDING        = 4;
constexpr uint32_t HDR_BINDING           = 5;

constexpr uint32_t GROUP_SIZE         = 8;
constexpr uint32_t RESOLUTION_DIVIDER = 2;

constexpr float SHAFTS_SCALE = 0.05f;
constexpr float GLARE_SCALE  = 0.05f;

constexpr float EDGE_FADE = 0.2f;

VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;
VkDescriptorPool m_pool = VK_NULL_HANDLE;

VkDescriptorSet m_shafts_set = VK_NULL_HANDLE;
VkDescriptorSet m_composite_set = VK_NULL_HANDLE;

VkSampler m_linear_sampler  = VK_NULL_HANDLE;
VkSampler m_nearest_sampler = VK_NULL_HANDLE;

image m_shafts;

VkImageView m_written_hdr_view = VK_NULL_HANDLE;
VkImageView m_written_clouds_view = VK_NULL_HANDLE;

const shader* m_shafts_shader = nullptr;
const shader* m_composite_shader = nullptr;

uint32_t get_groups(uint32_t size)
{
    return (size + GROUP_SIZE - 1) / GROUP_SIZE;
}

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

const shader* build_compute(const char* name, const char* file, uint32_t push_size)
{
    compute_pipeline_create_info info;
    info.set_layouts        = { m_set_layout };
    info.push_constant_size = push_size;

    return res::shaders::build(name, "", "", file, {}, info, render_queue::opaque);
}

void recreate_targets(const image& hdr)
{
    if (m_shafts.handle != VK_NULL_HANDLE) {
        vk::device::get_disp().deviceWaitIdle();

        vk::resources::destroy_image(m_shafts);
    }

    const VkExtent3D extent = {
        std::max(1U, (hdr.extent.width + RESOLUTION_DIVIDER - 1) / RESOLUTION_DIVIDER),
        std::max(1U, (hdr.extent.height + RESOLUTION_DIVIDER - 1) / RESOLUTION_DIVIDER),
        1
    };

    m_shafts = vk::resources::create_image(extent, HDR_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    vk::descriptors::write_image(m_shafts_set, DEPTH_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, it::get_depth_image().view, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, m_nearest_sampler);
    vk::descriptors::write_image(m_shafts_set, TARGET_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_shafts.view, VK_IMAGE_LAYOUT_GENERAL);

    vk::descriptors::write_image(m_composite_set, SHAFTS_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_shafts.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_linear_sampler);
    vk::descriptors::write_image(m_composite_set, HDR_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, hdr.view, VK_IMAGE_LAYOUT_GENERAL);
}

bool light_screen_position(glm::vec3 light_direction, glm::vec2& sun_uv, float& fade)
{
    const gpu_camera& camera = frame_pipe::get_camera();

    const glm::vec4 clip = camera.view_proj * glm::vec4(light_direction, 0.0f);

    if (clip.w <= 0.0f) {
        return false;
    }

    const glm::vec2 ndc = glm::vec2(clip.x, clip.y) / clip.w;

    const float edge = std::max(std::abs(ndc.x), std::abs(ndc.y));

    fade = glm::clamp((1.0f - edge) / EDGE_FADE, 0.0f, 1.0f);

    sun_uv = ndc * 0.5f + 0.5f;

    return fade > 0.0f;
}

}

void init_all()
{
    m_set_layout = vk::descriptors::create_set_layout({
        { DEPTH_BINDING,         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { CLOUDS_BINDING,        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { TRANSMITTANCE_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { SHAFTS_BINDING,        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { TARGET_BINDING,        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { HDR_BINDING,           VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    });

    m_pool = vk::descriptors::create_pool(
        2,
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          4 }
        }
    );

    m_shafts_set    = vk::descriptors::allocate_set(m_pool, m_set_layout);
    m_composite_set = vk::descriptors::allocate_set(m_pool, m_set_layout);

    m_linear_sampler  = make_sampler(VK_FILTER_LINEAR);
    m_nearest_sampler = make_sampler(VK_FILTER_NEAREST);

    vk::descriptors::write_image(
        m_shafts_set,
        TRANSMITTANCE_BINDING,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        atmosphere::get_transmittance_image().view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        m_linear_sampler
    );

    m_shafts_shader    = build_compute("light_shafts", "sys/post/light_shafts.comp", sizeof(shafts_push));
    m_composite_shader = build_compute("light_shafts_composite", "sys/post/light_shafts_composite.comp", 0);
}

void clean_all()
{
    vk::resources::destroy_image(m_shafts);

    vk::descriptors::destroy_pool(m_pool);
    vk::descriptors::destroy_set_layout(m_set_layout);

    m_set_layout = VK_NULL_HANDLE;
    m_pool       = VK_NULL_HANDLE;

    m_linear_sampler  = VK_NULL_HANDLE;
    m_nearest_sampler = VK_NULL_HANDLE;

    m_written_hdr_view    = VK_NULL_HANDLE;
    m_written_clouds_view = VK_NULL_HANDLE;

    m_shafts_shader    = nullptr;
    m_composite_shader = nullptr;
}

void handle_all()
{
    const image& hdr = it::get_output_image();

    if (hdr.view != m_written_hdr_view) {
        recreate_targets(hdr);

        m_written_hdr_view = hdr.view;
    }

    const VkImageView clouds_view = clouds::get_output_image().view;

    if (clouds_view != m_written_clouds_view) {
        vk::descriptors::write_image(m_shafts_set, CLOUDS_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, clouds_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_linear_sampler);

        m_written_clouds_view = clouds_view;
    }
}

void dispatch()
{
    const float intensity = frame_pipe::get_post().light_shafts_intensity;
    const float glare     = frame_pipe::get_post().sun_glare_intensity;

    if (!atmosphere::is_active() || (intensity <= 0.0f && glare <= 0.0f)) {
        return;
    }

    const gpu_atmosphere& atmosphere = frame_pipe::get_atmosphere();

    const glm::vec3 light_direction   = atmosphere.sun_direction;
    const glm::vec3 light_illuminance = atmosphere.sun_tint * atmosphere.sun_illuminance;

    if (light_direction.y <= 0.0f) {
        return;
    }

    glm::vec2 sun_uv;
    float fade = 0.0f;

    if (!light_screen_position(light_direction, sun_uv, fade)) {
        return;
    }

    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const image& hdr = it::get_output_image();

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );

    vk::commands::transition_image(
        cmd,
        m_shafts.handle,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        0,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    shafts_push push;
    push.params    = atmosphere::get_params_address();
    push.sun_uv    = sun_uv;
    push.intensity       = exposure::get_exposure() * SHAFTS_SCALE * intensity * fade;
    push.aspect          = static_cast<float>(hdr.extent.width) / static_cast<float>(hdr.extent.height);
    push.light_direction = glm::vec4(glm::normalize(light_direction), 0.0f);
    push.light_color     = glm::vec4(light_illuminance, 0.0f);
    push.glare           = exposure::get_exposure() * GLARE_SCALE * glare * fade;
    push.padding0        = 0.0f;

    disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_shafts_shader->compute.layout, 0, 1, &m_shafts_set, 0, nullptr);

    vk::commands::dispatch(cmd, m_shafts_shader->compute, &push, sizeof(push), get_groups(m_shafts.extent.width), get_groups(m_shafts.extent.height), 1);

    vk::commands::transition_image(
        cmd,
        m_shafts.handle,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );

    vk::commands::transition_image(
        cmd,
        hdr.handle,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        0,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_composite_shader->compute.layout, 0, 1, &m_composite_set, 0, nullptr);

    vk::commands::dispatch(cmd, m_composite_shader->compute, nullptr, 0, get_groups(hdr.extent.width), get_groups(hdr.extent.height), 1);

    vk::commands::transition_image(
        cmd,
        hdr.handle,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );
}

}
