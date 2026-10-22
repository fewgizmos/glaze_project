#include "glp/glaze/rendering/scene_render/clouds.h"

#include "glp/glaze/rendering/scene_render/atmosphere.h"
#include "glp/glaze/rendering/scene_render/environment.h"
#include "glp/glaze/rendering/scene_render/frame_pipeline.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include <vulkan/vulkan_core.h>

#include <glm/common.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace glaze::rendering::scene_render::clouds
{

namespace
{

struct trace_push
{
    VkDeviceAddress params;
    float quality;
    uint32_t cell;
};

struct shadow_key
{
    glm::vec3 sun_direction;
    glm::vec3 moon_direction;
    glm::vec2 camera_cell;

    bool enabled;

    float bottom;
    float thickness;
    float coverage;
    float density;
    float type;
};

struct trace_key
{
    glm::mat4 view_proj;
    glm::vec3 camera;

    glm::vec3 sun_light;
    glm::vec3 moon_light;

    shadow_key shadow;
};

struct resolve_push
{
    VkDeviceAddress params;
    uint32_t reset;
    float history_weight;
    uint32_t cell;
    uint32_t padding0;
};

constexpr VkExtent3D SHAPE_NOISE_SIZE  = { 256, 256, 256 };
constexpr VkExtent3D DETAIL_NOISE_SIZE = { 64, 64, 64 };
constexpr VkExtent3D WEATHER_MAP_SIZE  = { 1024, 1024, 1 };

constexpr VkExtent3D SHADOW_MAP_SIZE = { 512, 512, 1 };

constexpr uint32_t SHADOW_MAP_LAYERS = 2;

constexpr float SHADOW_MAP_TEXEL = 16000.0f / 512.0f;

constexpr uint32_t SETTLE_FRAMES = 256;

constexpr VkFormat SHADOW_FORMAT = VK_FORMAT_R16_SFLOAT;

constexpr VkFormat NOISE_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat TRACE_FORMAT = VK_FORMAT_R32G32B32A32_SFLOAT;
constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_R32_SFLOAT;

constexpr uint32_t GROUP_SIZE        = 8;
constexpr uint32_t VOLUME_GROUP_SIZE = 4;

constexpr uint32_t RESOLUTION_DIVIDER = 1;
constexpr uint32_t CHECKERBOARD_CELL = 4;
constexpr uint32_t FULL_CELL         = 1;

constexpr float HISTORY_WEIGHT = 0.9f;

constexpr uint32_t TRANSMITTANCE_BINDING    = 0;
constexpr uint32_t MULTI_SCATTERING_BINDING = 1;
constexpr uint32_t SKY_VIEW_BINDING         = 2;
constexpr uint32_t SHAPE_BINDING            = 3;
constexpr uint32_t DETAIL_BINDING           = 4;
constexpr uint32_t WEATHER_BINDING          = 5;
constexpr uint32_t HISTORY_BINDING          = 6;
constexpr uint32_t TARGET_BINDING           = 7;
constexpr uint32_t DEPTH_BINDING            = 8;
constexpr uint32_t TRACE_BINDING            = 9;

constexpr uint32_t SAMPLER_BINDINGS = 7;
constexpr uint32_t STORAGE_BINDINGS = 3;

constexpr uint32_t SETS_COUNT = 6;

VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;
VkDescriptorPool m_pool = VK_NULL_HANDLE;

VkDescriptorSet m_shape_set = VK_NULL_HANDLE;
VkDescriptorSet m_detail_set = VK_NULL_HANDLE;
VkDescriptorSet m_weather_set = VK_NULL_HANDLE;
VkDescriptorSet m_trace_set = VK_NULL_HANDLE;
VkDescriptorSet m_resolve_set = VK_NULL_HANDLE;
VkDescriptorSet m_shadow_set = VK_NULL_HANDLE;

VkSampler m_noise_sampler = VK_NULL_HANDLE;
VkSampler m_clamp_sampler = VK_NULL_HANDLE;

image m_shape_noise;
image m_detail_noise;
image m_weather_map;

image m_shadow_map;

image m_trace;
image m_depth;
image m_output;
image m_history;

const shader* m_shape_shader = nullptr;
const shader* m_detail_shader = nullptr;
const shader* m_weather_shader = nullptr;
const shader* m_trace_shader = nullptr;
const shader* m_resolve_shader = nullptr;
const shader* m_shadow_shader = nullptr;

shadow_key m_shadow_key = {};

trace_key m_trace_key = {};

uint32_t m_static_frames = 0;

bool m_shadow_valid = false;

bool m_history_valid = false;

bool m_trace_initialized = false;

uint32_t get_groups(uint32_t size, uint32_t group)
{
    return (size + group - 1) / group;
}

VkSampler make_sampler(VkSamplerAddressMode wrap)
{
    sampler_create_info sampler_info;
    sampler_info.mip_mode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.wrap_u     = wrap;
    sampler_info.wrap_v     = wrap;
    sampler_info.wrap_w     = wrap;
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

void write_sampled(VkDescriptorSet set, uint32_t binding, const image& source, VkSampler sampler)
{
    vk::descriptors::write_image(set, binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, source.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, sampler);
}

void write_storage(VkDescriptorSet set, uint32_t binding, const image& target)
{
    vk::descriptors::write_image(set, binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, target.view, VK_IMAGE_LAYOUT_GENERAL);
}

void create_layout_and_sets()
{
    m_set_layout = vk::descriptors::create_set_layout({
        { TRANSMITTANCE_BINDING,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { MULTI_SCATTERING_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { SKY_VIEW_BINDING,         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { SHAPE_BINDING,            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { DETAIL_BINDING,           VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { WEATHER_BINDING,          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { HISTORY_BINDING,          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { TARGET_BINDING,           VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { DEPTH_BINDING,            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { TRACE_BINDING,            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    });

    m_pool = vk::descriptors::create_pool(
        SETS_COUNT,
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SETS_COUNT * SAMPLER_BINDINGS },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          SETS_COUNT * STORAGE_BINDINGS }
        }
    );

    m_shape_set   = vk::descriptors::allocate_set(m_pool, m_set_layout);
    m_detail_set  = vk::descriptors::allocate_set(m_pool, m_set_layout);
    m_weather_set = vk::descriptors::allocate_set(m_pool, m_set_layout);
    m_trace_set   = vk::descriptors::allocate_set(m_pool, m_set_layout);
    m_resolve_set = vk::descriptors::allocate_set(m_pool, m_set_layout);
    m_shadow_set  = vk::descriptors::allocate_set(m_pool, m_set_layout);

    write_storage(m_shape_set, TARGET_BINDING, m_shape_noise);
    write_storage(m_detail_set, TARGET_BINDING, m_detail_noise);
    write_storage(m_weather_set, TARGET_BINDING, m_weather_map);

    write_sampled(m_trace_set, TRANSMITTANCE_BINDING, atmosphere::get_transmittance_image(), m_clamp_sampler);
    write_sampled(m_trace_set, MULTI_SCATTERING_BINDING, atmosphere::get_multi_scattering_image(), m_clamp_sampler);
    write_sampled(m_trace_set, SKY_VIEW_BINDING, atmosphere::get_sky_view_image(), m_clamp_sampler);
    write_sampled(m_trace_set, SHAPE_BINDING, m_shape_noise, m_noise_sampler);
    write_sampled(m_trace_set, DETAIL_BINDING, m_detail_noise, m_noise_sampler);
    write_sampled(m_trace_set, WEATHER_BINDING, m_weather_map, m_noise_sampler);

    write_sampled(m_shadow_set, SHAPE_BINDING, m_shape_noise, m_noise_sampler);
    write_sampled(m_shadow_set, DETAIL_BINDING, m_detail_noise, m_noise_sampler);
    write_sampled(m_shadow_set, WEATHER_BINDING, m_weather_map, m_noise_sampler);
    write_storage(m_shadow_set, TARGET_BINDING, m_shadow_map);
}

void create_noise_images()
{
    m_shape_noise  = vk::resources::create_volume_image(SHAPE_NOISE_SIZE, NOISE_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    m_detail_noise = vk::resources::create_volume_image(DETAIL_NOISE_SIZE, NOISE_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    m_weather_map  = vk::resources::create_image(WEATHER_MAP_SIZE, NOISE_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    m_shadow_map = vk::resources::create_image(
        SHADOW_MAP_SIZE,
        SHADOW_FORMAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1,
        SHADOW_MAP_LAYERS
    );
}

void clear_shadow_map()
{
    vk::commands::immediate_submit([](VkCommandBuffer cmd) {
        vk::commands::transition_image(
            cmd,
            m_shadow_map.handle,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE,
            0,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1,
            SHADOW_MAP_LAYERS
        );

        const VkClearColorValue clear_value = { { 1.0f, 1.0f, 1.0f, 1.0f } };

        VkImageSubresourceRange range = {};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = SHADOW_MAP_LAYERS;

        vk::device::get_disp().cmdClearColorImage(cmd, m_shadow_map.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_value, 1, &range);

        vk::commands::transition_image(
            cmd,
            m_shadow_map.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1,
            SHADOW_MAP_LAYERS
        );
    });
}

shadow_key make_shadow_key()
{
    const gpu_atmosphere& atmosphere = frame_pipe::get_atmosphere();
    const glm::vec3 camera           = frame_pipe::get_camera().position;

    shadow_key key = {};

    key.sun_direction  = atmosphere.sun_direction;
    key.moon_direction = atmosphere.moon_direction;
    key.camera_cell    = glm::floor(glm::vec2(camera.x, camera.z) / SHADOW_MAP_TEXEL);
    key.enabled        = atmosphere.clouds_enabled;
    key.bottom         = atmosphere.cloud_bottom;
    key.thickness      = atmosphere.cloud_thickness;
    key.coverage       = atmosphere.cloud_coverage;
    key.density        = atmosphere.cloud_density;
    key.type           = atmosphere.cloud_type;

    return key;
}

bool same_shadow_key(const shadow_key& left, const shadow_key& right)
{
    return left.sun_direction == right.sun_direction
        && left.moon_direction == right.moon_direction
        && left.camera_cell == right.camera_cell
        && left.enabled == right.enabled
        && left.bottom == right.bottom
        && left.thickness == right.thickness
        && left.coverage == right.coverage
        && left.density == right.density
        && left.type == right.type;
}

uint32_t choose_trace_cell()
{
    return m_history_valid ? CHECKERBOARD_CELL : FULL_CELL;
}

bool trace_is_static()
{
    const gpu_camera& camera         = frame_pipe::get_camera();
    const gpu_atmosphere& atmosphere = frame_pipe::get_atmosphere();

    trace_key key = {};

    key.view_proj  = camera.view_proj;
    key.camera     = camera.position;
    key.sun_light  = atmosphere.sun_light;
    key.moon_light = atmosphere.moon_light;
    key.shadow     = make_shadow_key();

    const bool same = std::memcmp(&key.view_proj, &m_trace_key.view_proj, sizeof(glm::mat4)) == 0
        && key.camera == m_trace_key.camera
        && key.sun_light == m_trace_key.sun_light
        && key.moon_light == m_trace_key.moon_light
        && same_shadow_key(key.shadow, m_trace_key.shadow);

    m_trace_key     = key;
    m_static_frames = same ? std::min(m_static_frames + 1, SETTLE_FRAMES + 1) : 0;

    return m_static_frames > SETTLE_FRAMES;
}

void record_shadow_map(VkCommandBuffer cmd)
{
    const shadow_key key = make_shadow_key();

    if (m_shadow_valid && same_shadow_key(key, m_shadow_key)) {
        return;
    }

    m_shadow_key   = key;
    m_shadow_valid = true;

    vk::commands::transition_image(
        cmd,
        m_shadow_map.handle,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        0,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1,
        SHADOW_MAP_LAYERS
    );

    const trace_push push = { atmosphere::get_params_address(), it::get_quality(), 0 };

    vk::device::get_disp().cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_shadow_shader->compute.layout, 0, 1, &m_shadow_set, 0, nullptr);

    vk::commands::dispatch(
        cmd,
        m_shadow_shader->compute,
        &push,
        sizeof(push),
        get_groups(SHADOW_MAP_SIZE.width, GROUP_SIZE),
        get_groups(SHADOW_MAP_SIZE.height, GROUP_SIZE),
        SHADOW_MAP_LAYERS
    );

    vk::commands::transition_image(
        cmd,
        m_shadow_map.handle,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1,
        SHADOW_MAP_LAYERS
    );
}

void create_shaders()
{
    m_shape_shader   = build_compute("clouds_shape_noise", "sys/bake/clouds_shape_noise.comp", 0);
    m_detail_shader  = build_compute("clouds_detail_noise", "sys/bake/clouds_detail_noise.comp", 0);
    m_weather_shader = build_compute("clouds_weather", "sys/bake/clouds_weather.comp", 0);
    m_trace_shader   = build_compute("clouds_trace", "sys/frame/clouds_trace.comp", sizeof(trace_push));
    m_resolve_shader = build_compute("clouds_resolve", "sys/frame/clouds_resolve.comp", sizeof(resolve_push));
    m_shadow_shader  = build_compute("clouds_shadow", "sys/frame/clouds_shadow.comp", sizeof(trace_push));
}

void record_generation(VkCommandBuffer cmd, const shader* target_shader, VkDescriptorSet set, const image& target, uint32_t group)
{
    environment::transition_bake_target(cmd, target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    vk::device::get_disp().cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, target_shader->compute.layout, 0, 1, &set, 0, nullptr);

    vk::commands::dispatch(
        cmd,
        target_shader->compute,
        nullptr,
        0,
        get_groups(target.extent.width, group),
        get_groups(target.extent.height, group),
        get_groups(target.extent.depth, group)
    );

    environment::transition_bake_target(cmd, target, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void generate_noise()
{
    vk::commands::immediate_submit([](VkCommandBuffer cmd) {
        record_generation(cmd, m_shape_shader, m_shape_set, m_shape_noise, VOLUME_GROUP_SIZE);
        record_generation(cmd, m_detail_shader, m_detail_set, m_detail_noise, VOLUME_GROUP_SIZE);
        record_generation(cmd, m_weather_shader, m_weather_set, m_weather_map, GROUP_SIZE);
    });
}

void destroy_targets()
{
    vk::resources::destroy_image(m_history);
    vk::resources::destroy_image(m_output);
    vk::resources::destroy_image(m_depth);
    vk::resources::destroy_image(m_trace);
}

void create_targets(VkExtent3D extent)
{
    m_trace   = vk::resources::create_image(extent, TRACE_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    m_depth   = vk::resources::create_image(extent, DEPTH_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    m_output  = vk::resources::create_image(extent, TRACE_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    m_history = vk::resources::create_image(extent, TRACE_FORMAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    write_storage(m_trace_set, TARGET_BINDING, m_trace);
    write_storage(m_trace_set, DEPTH_BINDING, m_depth);

    write_sampled(m_resolve_set, HISTORY_BINDING, m_history, m_clamp_sampler);
    write_storage(m_resolve_set, TARGET_BINDING, m_output);
    write_storage(m_resolve_set, DEPTH_BINDING, m_depth);
    write_storage(m_resolve_set, TRACE_BINDING, m_trace);

    m_history_valid     = false;
    m_trace_initialized = false;
}

void transition_to_general(VkCommandBuffer cmd, const image& target)
{
    vk::commands::transition_image(
        cmd,
        target.handle,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        0,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );
}

void record_history_copy(VkCommandBuffer cmd)
{
    vk::commands::transition_image(
        cmd,
        m_output.handle,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT
    );

    vk::commands::transition_image(
        cmd,
        m_history.handle,
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
    region.extent = m_output.extent;

    vk::device::get_disp().cmdCopyImage(
        cmd,
        m_output.handle,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        m_history.handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    vk::commands::transition_image(
        cmd,
        m_output.handle,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        0,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );
}

}

void init_all()
{
    m_noise_sampler = make_sampler(VK_SAMPLER_ADDRESS_MODE_REPEAT);
    m_clamp_sampler = make_sampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    create_noise_images();

    create_layout_and_sets();

    create_shaders();

    generate_noise();

    clear_shadow_map();
}

void clean_all()
{
    destroy_targets();

    vk::resources::destroy_image(m_shadow_map);
    vk::resources::destroy_image(m_weather_map);
    vk::resources::destroy_image(m_detail_noise);
    vk::resources::destroy_image(m_shape_noise);

    vk::descriptors::destroy_pool(m_pool);
    vk::descriptors::destroy_set_layout(m_set_layout);

    m_set_layout = VK_NULL_HANDLE;
    m_pool       = VK_NULL_HANDLE;

    m_noise_sampler = VK_NULL_HANDLE;
    m_clamp_sampler = VK_NULL_HANDLE;

    m_history_valid     = false;
    m_trace_initialized = false;
    m_shadow_valid      = false;
}

void handle_all()
{
    const VkExtent2D extent = it::get_render_extent();

    const VkExtent3D target_extent = {
        std::max(1U, (extent.width + RESOLUTION_DIVIDER - 1) / RESOLUTION_DIVIDER),
        std::max(1U, (extent.height + RESOLUTION_DIVIDER - 1) / RESOLUTION_DIVIDER),
        1
    };

    if (m_trace.extent.width == target_extent.width && m_trace.extent.height == target_extent.height) {
        return;
    }

    if (m_trace.handle != VK_NULL_HANDLE) {
        vk::device::get_disp().deviceWaitIdle();

        destroy_targets();
    }

    create_targets(target_extent);
}

void dispatch()
{
    if (!atmosphere::is_active()) {
        return;
    }

    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    record_shadow_map(cmd);

    if (m_trace_initialized && m_history_valid && trace_is_static()) {
        return;
    }

    if (!m_trace_initialized) {
        transition_to_general(cmd, m_trace);
        transition_to_general(cmd, m_depth);

        m_trace_initialized = true;
    } else {
        vk::commands::memory_barrier(
            cmd,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
        );
    }

    const uint32_t trace_cell = choose_trace_cell();

    const trace_push trace = { atmosphere::get_params_address(), it::get_quality(), trace_cell };

    disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_trace_shader->compute.layout, 0, 1, &m_trace_set, 0, nullptr);

    vk::commands::dispatch(
        cmd,
        m_trace_shader->compute,
        &trace,
        sizeof(trace),
        get_groups(get_groups(m_trace.extent.width, trace_cell), GROUP_SIZE),
        get_groups(get_groups(m_trace.extent.height, trace_cell), GROUP_SIZE),
        1
    );

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT
    );

    vk::commands::transition_image(
        cmd,
        m_history.handle,
        m_history_valid ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );

    transition_to_general(cmd, m_output);

    resolve_push resolve;
    resolve.params         = atmosphere::get_params_address();
    resolve.reset          = m_history_valid ? 0 : 1;
    resolve.history_weight = HISTORY_WEIGHT;
    resolve.cell           = trace_cell;
    resolve.padding0       = 0;

    disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_resolve_shader->compute.layout, 0, 1, &m_resolve_set, 0, nullptr);

    vk::commands::dispatch(
        cmd,
        m_resolve_shader->compute,
        &resolve,
        sizeof(resolve),
        get_groups(m_output.extent.width, GROUP_SIZE),
        get_groups(m_output.extent.height, GROUP_SIZE),
        1
    );

    record_history_copy(cmd);

    m_history_valid = true;
}

const image& get_output_image() { return m_output; }

const image& get_shadow_image() { return m_shadow_map; }

}
