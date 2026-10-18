#include "glp/glaze/rendering/scene_render/volumetric_fog.h"

#include "glp/glaze/rendering/scene_render/atmosphere.h"
#include "glp/glaze/rendering/scene_render/clouds.h"
#include "glp/glaze/rendering/scene_render/frame_pipeline.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"
#include "glp/glaze/rendering/scene_render/shadows.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <cstdint>

namespace glaze::rendering::scene_render::volumetric_fog
{

namespace
{

struct params_data
{
    glm::mat4 inv_view_proj;
    glm::mat4 previous_view_proj;

    glm::vec3 camera_pos;
    float near_distance;

    glm::vec3 previous_camera_pos;
    float far_distance;

    float jitter;
    float history_weight;
    uint32_t reset;
    uint32_t parity;
};

struct fog_push
{
    VkDeviceAddress frame;
    VkDeviceAddress params;
};

constexpr VkExtent3D GRID_SIZE = { 160, 90, 128 };

constexpr VkFormat VOLUME_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;

constexpr uint32_t INJECT_GROUP_SIZE    = 4;
constexpr uint32_t INTEGRATE_GROUP_SIZE = 8;

constexpr uint32_t VOLUME_COUNT = 2;

constexpr float NEAR_DISTANCE  = 0.5f;
constexpr float HISTORY_WEIGHT = 0.9f;

constexpr float MIN_OPTICAL_DEPTH = 0.05f;

constexpr uint32_t JITTER_SEQUENCE_LENGTH = 16;
constexpr uint32_t JITTER_BASE            = 2;

constexpr uint32_t HISTORY_BINDING      = 0;
constexpr uint32_t TARGET_BINDING       = 1;
constexpr uint32_t CLOUD_SHADOW_BINDING = 2;
constexpr uint32_t SHADOW_POOL_BINDING  = 3;

image m_scattering[VOLUME_COUNT];
image m_integrated;

buffer m_params_buffers[MAX_FRAMES_IN_FLIGHT];

VkDescriptorSetLayout m_inject_layout = VK_NULL_HANDLE;
VkDescriptorSetLayout m_integrate_layout = VK_NULL_HANDLE;
VkDescriptorPool m_pool = VK_NULL_HANDLE;

VkDescriptorSet m_inject_sets[VOLUME_COUNT] = {};
VkDescriptorSet m_integrate_sets[VOLUME_COUNT] = {};

VkSampler m_linear_sampler = VK_NULL_HANDLE;
VkSampler m_nearest_sampler = VK_NULL_HANDLE;

const shader* m_inject_shader = nullptr;
const shader* m_integrate_shader = nullptr;

glm::mat4 m_previous_view_proj = glm::mat4(1.0f);
glm::vec3 m_previous_camera_pos = glm::vec3(0.0f);

uint32_t m_current = 0;
uint32_t m_frame_index = 0;

bool m_active = false;
bool m_history_valid = false;

uint32_t get_groups(uint32_t size, uint32_t group)
{
    return (size + group - 1) / group;
}

float halton(uint32_t index, uint32_t base)
{
    float result   = 0.0f;
    float fraction = 1.0f;

    for (; index > 0; index /= base) {
        fraction /= static_cast<float>(base);
        result   += fraction * static_cast<float>(index % base);
    }

    return result;
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

void write_sampled(VkDescriptorSet set, uint32_t binding, VkImageView view, VkImageLayout layout, VkSampler sampler)
{
    vk::descriptors::write_image(set, binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, view, layout, sampler);
}

void write_storage(VkDescriptorSet set, uint32_t binding, const image& target)
{
    vk::descriptors::write_image(set, binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, target.view, VK_IMAGE_LAYOUT_GENERAL);
}

void create_sets()
{
    m_inject_layout = vk::descriptors::create_set_layout({
        { HISTORY_BINDING,      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { TARGET_BINDING,       VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { CLOUD_SHADOW_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { SHADOW_POOL_BINDING,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    });

    m_integrate_layout = vk::descriptors::create_set_layout({
        { HISTORY_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { TARGET_BINDING,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    });

    m_pool = vk::descriptors::create_pool(
        VOLUME_COUNT * 2,
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VOLUME_COUNT * 4 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VOLUME_COUNT * 2 }
        }
    );

    for (uint32_t i = 0; i < VOLUME_COUNT; i++) {
        const uint32_t history = (i + 1) % VOLUME_COUNT;

        m_inject_sets[i]    = vk::descriptors::allocate_set(m_pool, m_inject_layout);
        m_integrate_sets[i] = vk::descriptors::allocate_set(m_pool, m_integrate_layout);

        write_sampled(m_inject_sets[i], HISTORY_BINDING, m_scattering[history].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_linear_sampler);
        write_storage(m_inject_sets[i], TARGET_BINDING, m_scattering[i]);
        write_sampled(m_inject_sets[i], CLOUD_SHADOW_BINDING, clouds::get_shadow_image().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_linear_sampler);
        write_sampled(m_inject_sets[i], SHADOW_POOL_BINDING, shadows::get_pool_image().view, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, m_nearest_sampler);

        write_sampled(m_integrate_sets[i], HISTORY_BINDING, m_scattering[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_nearest_sampler);
        write_storage(m_integrate_sets[i], TARGET_BINDING, m_integrated);
    }
}

void initialize_volumes()
{
    vk::commands::immediate_submit([](VkCommandBuffer cmd) {
        for (uint32_t i = 0; i < VOLUME_COUNT; i++) {
            vk::commands::transition_image(
                cmd,
                m_scattering[i].handle,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE,
                0,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            );
        }

        vk::commands::transition_image(
            cmd,
            m_integrated.handle,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE,
            0,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
        );
    });
}

void write_params()
{
    const gpu_camera& camera = frame_pipe::get_camera();
    const gpu_post& post     = frame_pipe::get_post();

    params_data data = {};

    data.inv_view_proj       = camera.inv_view_proj;
    data.previous_view_proj  = m_history_valid ? m_previous_view_proj : camera.view_proj;
    data.camera_pos          = camera.position;
    data.near_distance       = NEAR_DISTANCE;
    data.previous_camera_pos = m_history_valid ? m_previous_camera_pos : camera.position;
    data.far_distance        = post.volumetric_fog_distance;
    data.jitter              = halton(m_frame_index % JITTER_SEQUENCE_LENGTH + 1, JITTER_BASE);
    data.history_weight      = HISTORY_WEIGHT;
    data.reset               = m_history_valid ? 0 : 1;
    data.parity              = m_frame_index % 2;

    vk::resources::upload_buffer(m_params_buffers[vk::frame::get_current_frame_index()], &data, sizeof(data));

    m_previous_view_proj  = camera.view_proj;
    m_previous_camera_pos = camera.position;
}

void run(VkCommandBuffer cmd, const shader* target, VkDescriptorSet set, uint32_t groups_x, uint32_t groups_y, uint32_t groups_z)
{
    const fog_push push = { it::get_frame_data_address(), get_params_address() };

    vk::device::get_disp().cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, target->compute.layout, 0, 1, &set, 0, nullptr);

    vk::commands::dispatch(cmd, target->compute, &push, sizeof(push), groups_x, groups_y, groups_z);
}

bool fog_visible()
{
    const gpu_atmosphere& atmosphere = frame_pipe::get_atmosphere();

    const float distance = frame_pipe::get_post().volumetric_fog_distance;
    const float lowest   = frame_pipe::get_camera().position.y - distance;

    const float density = atmosphere::get_fog_density() * std::exp(-atmosphere.fog_height_falloff * (lowest - atmosphere.fog_base_height));

    return density * distance >= MIN_OPTICAL_DEPTH;
}

}

void init_all()
{
    for (uint32_t i = 0; i < VOLUME_COUNT; i++) {
        m_scattering[i] = vk::resources::create_volume_image(GRID_SIZE, VOLUME_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    m_integrated = vk::resources::create_volume_image(GRID_SIZE, VOLUME_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_params_buffers[i] = vk::resources::create_buffer(
            sizeof(params_data),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        );
    }

    m_linear_sampler  = make_sampler(VK_FILTER_LINEAR);
    m_nearest_sampler = make_sampler(VK_FILTER_NEAREST);

    create_sets();

    initialize_volumes();

    compute_pipeline_create_info inject_info;
    inject_info.set_layouts        = { m_inject_layout };
    inject_info.push_constant_size = sizeof(fog_push);

    compute_pipeline_create_info integrate_info;
    integrate_info.set_layouts        = { m_integrate_layout };
    integrate_info.push_constant_size = sizeof(fog_push);

    m_inject_shader    = res::shaders::build("fog_inject", "", "", "sys/frame/fog_inject.comp", {}, inject_info, render_queue::opaque);
    m_integrate_shader = res::shaders::build("fog_integrate", "", "", "sys/frame/fog_integrate.comp", {}, integrate_info, render_queue::opaque);
}

void clean_all()
{
    vk::descriptors::destroy_pool(m_pool);
    vk::descriptors::destroy_set_layout(m_integrate_layout);
    vk::descriptors::destroy_set_layout(m_inject_layout);

    m_pool             = VK_NULL_HANDLE;
    m_inject_layout    = VK_NULL_HANDLE;
    m_integrate_layout = VK_NULL_HANDLE;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::resources::destroy_buffer(m_params_buffers[i]);
    }

    vk::resources::destroy_image(m_integrated);

    for (uint32_t i = 0; i < VOLUME_COUNT; i++) {
        vk::resources::destroy_image(m_scattering[i]);
    }

    m_linear_sampler  = VK_NULL_HANDLE;
    m_nearest_sampler = VK_NULL_HANDLE;

    m_active        = false;
    m_history_valid = false;
}

void handle_all()
{
    const bool active = frame_pipe::has_camera()
                     && frame_pipe::has_atmosphere()
                     && frame_pipe::get_post().volumetric_fog_enabled
                     && fog_visible();

    if (!active) {
        m_active        = false;
        m_history_valid = false;
        return;
    }

    m_active = true;

    write_params();
}

void dispatch()
{
    if (!m_active) {
        return;
    }

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const image& current = m_scattering[m_current];

    vk::commands::transition_image(
        cmd,
        current.handle,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        0,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    run(
        cmd,
        m_inject_shader,
        m_inject_sets[m_current],
        get_groups(GRID_SIZE.width, INJECT_GROUP_SIZE),
        get_groups(GRID_SIZE.height, INJECT_GROUP_SIZE),
        get_groups(GRID_SIZE.depth, INJECT_GROUP_SIZE)
    );

    vk::commands::transition_image(
        cmd,
        current.handle,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );

    vk::commands::transition_image(
        cmd,
        m_integrated.handle,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        0,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    run(
        cmd,
        m_integrate_shader,
        m_integrate_sets[m_current],
        get_groups(GRID_SIZE.width, INTEGRATE_GROUP_SIZE),
        get_groups(GRID_SIZE.height, INTEGRATE_GROUP_SIZE),
        1
    );

    vk::commands::transition_image(
        cmd,
        m_integrated.handle,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );

    m_current       = (m_current + 1) % VOLUME_COUNT;
    m_frame_index   = m_frame_index + 1;
    m_history_valid = true;
}

bool is_active() { return m_active; }

VkDeviceAddress get_params_address()
{
    return m_params_buffers[vk::frame::get_current_frame_index()].address;
}

const image& get_volume_image() { return m_integrated; }

}
