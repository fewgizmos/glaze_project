#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/runtime/config.h"

#include "glp/glaze/rendering/scene_render/atmosphere.h"
#include "glp/glaze/rendering/scene_render/bloom.h"
#include "glp/glaze/rendering/scene_render/clouds.h"
#include "glp/glaze/rendering/scene_render/exposure.h"
#include "glp/glaze/rendering/scene_render/light_shafts.h"
#include "glp/glaze/rendering/scene_render/environment.h"
#include "glp/glaze/rendering/scene_render/frame_pipeline.h"
#include "glp/glaze/rendering/scene_render/fsr.h"
#include "glp/glaze/rendering/scene_render/gtao.h"

#ifdef DEV_BUILD
#include "glp/glaze/rendering/scene_render/outline_mask.h"
#endif

#include "glp/glaze/rendering/scene_render/picking.h"
#include "glp/glaze/rendering/scene_render/post_process.h"
#include "glp/glaze/rendering/scene_render/shadows.h"
#include "glp/glaze/rendering/scene_render/volumetric_fog.h"

#include "glp/glaze/rendering/scene_render/resources/gpu_environment.h"
#include "glp/glaze/rendering/scene_render/resources/materials.h"
#include "glp/glaze/rendering/scene_render/resources/meshes.h"
#include "glp/glaze/rendering/scene_render/resources/models.h"
#include "glp/glaze/rendering/scene_render/resources/shaders.h"
#include "glp/glaze/rendering/scene_render/resources/textures.h"

#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/samplers.h"

#include "glp/glaze/rendering/vk/profiler.h"

#include "glp/glaze/rendering/defines.h"
#include "glp/glaze/rendering/queue_deletor.h"

#include "glp/glaze/utilities/core/timer.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cstdint>

namespace glaze::rendering::scene_render::it
{

namespace
{

struct frame_scene_data
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 view_proj;
    glm::mat4 inv_view_proj;

    glm::vec3 camera_pos;
    float timestamp;

    uint32_t light_count;
    uint32_t has_environment;
    float environment_intensity;
    float quality;

    VkDeviceAddress objects;
    VkDeviceAddress materials;
    VkDeviceAddress lights;

    uint32_t frame_index;
    float exposure;

    glm::mat4 unjittered_view_proj;
    glm::mat4 previous_view_proj;

    VkDeviceAddress environment_sh;
    VkDeviceAddress atmosphere;

    uint32_t has_atmosphere;
    uint32_t has_shadows;

    VkDeviceAddress shadows;
    VkDeviceAddress fog;

    uint32_t has_fog;
    float texture_lod_bias;
};

constexpr uint32_t GTAO_BINDING         = 2;
constexpr uint32_t SHADOW_BINDING       = 3;
constexpr uint32_t CLOUD_SHADOW_BINDING = 4;
constexpr uint32_t SHADOW_MASK_BINDING  = 5;
constexpr uint32_t FOG_BINDING          = 6;

constexpr uint32_t COMBINED_BINDINGS = 5;

constexpr float MIN_QUALITY             = 0.5f;
constexpr float MAX_QUALITY             = 1.0f;
constexpr float QUALITY_STEP_DOWN       = 0.02f;
constexpr float QUALITY_STEP_UP         = 0.005f;
constexpr float QUALITY_RAISE_THRESHOLD = 0.85f;

float m_quality = MAX_QUALITY;

image m_depth_image;
image m_hdr_image;
image m_normal_image;
image m_velocity_image;
image m_gtao_image;
image m_output_image;

VkExtent2D m_display_extent = {};
uint32_t m_upscale_mode = 0;

glm::vec2 m_jitter_offset = glm::vec2(0.0f);

buffer m_frame_buffers[MAX_FRAMES_IN_FLIGHT];

VkDescriptorSetLayout m_scene_set_layout = VK_NULL_HANDLE;
VkDescriptorPool m_scene_pool = VK_NULL_HANDLE;
VkDescriptorSet m_scene_set = VK_NULL_HANDLE;

uint32_t m_bound_samplers = 0;

VkImageView m_written_gtao_view = VK_NULL_HANDLE;
VkImageView m_written_shadow_view = VK_NULL_HANDLE;
VkImageView m_written_cloud_shadow_view = VK_NULL_HANDLE;
VkImageView m_written_shadow_mask_view = VK_NULL_HANDLE;
VkImageView m_written_fog_view = VK_NULL_HANDLE;

VkSampler m_linear_sampler = VK_NULL_HANDLE;
VkSampler m_gtao_sampler = VK_NULL_HANDLE;

uint32_t m_frame_index = 0;

glm::mat4 m_previous_view_proj = glm::mat4(1.0f);
bool m_has_previous_view_proj = false;

image create_target(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage)
{
    return vk::resources::create_image({ extent.width, extent.height, 1 }, format, usage, VK_IMAGE_ASPECT_COLOR_BIT);
}

void create_render_targets()
{
    m_display_extent = vk::frame::get_swapchain().extent;
    m_upscale_mode   = frame_pipe::get_post().upscale_mode;

    const VkExtent2D extent = fsr::get_render_extent(m_display_extent, m_upscale_mode);

    m_depth_image = vk::resources::create_image(
        { extent.width, extent.height, 1 },
        DEPTH_FORMAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    m_hdr_image      = create_target(extent, HDR_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    m_normal_image   = create_target(extent, GBUFFER_NORMAL_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    m_velocity_image = create_target(extent, GBUFFER_VELOCITY_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    m_gtao_image     = create_target(extent, GTAO_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    m_output_image = create_target(m_display_extent, HDR_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
}

void destroy_render_targets()
{
    vk::resources::destroy_image(m_output_image);
    vk::resources::destroy_image(m_gtao_image);
    vk::resources::destroy_image(m_velocity_image);
    vk::resources::destroy_image(m_normal_image);
    vk::resources::destroy_image(m_hdr_image);
    vk::resources::destroy_image(m_depth_image);
}

void ensure_render_targets()
{
    const VkExtent2D extent = vk::frame::get_swapchain().extent;

    const bool same_display = m_display_extent.width == extent.width && m_display_extent.height == extent.height;

    if (same_display && m_upscale_mode == frame_pipe::get_post().upscale_mode) {
        return;
    }

    vk::device::get_disp().deviceWaitIdle();

    destroy_render_targets();

    create_render_targets();
}

void create_frame_buffers()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_frame_buffers[i] = vk::resources::create_buffer(
            sizeof(frame_scene_data),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        );
    }
}

void destroy_frame_buffers()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::resources::destroy_buffer(m_frame_buffers[i]);
    }
}

void create_scene_set()
{
    const VkDescriptorBindingFlags flags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

    m_scene_set_layout = vk::descriptors::create_set_layout(
        {
            { 0,                    VK_DESCRIPTOR_TYPE_SAMPLER,                MAX_SAMPLERS, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { 1,                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          MAX_TEXTURES, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { GTAO_BINDING,         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,            VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { SHADOW_BINDING,       VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,            VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { CLOUD_SHADOW_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,            VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { SHADOW_MASK_BINDING,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,            VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { FOG_BINDING,          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,            VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
        },
        { flags, flags, flags, flags, flags, flags, flags }
    );

    m_scene_pool = vk::descriptors::create_pool(
        1,
        {
            { VK_DESCRIPTOR_TYPE_SAMPLER,                MAX_SAMPLERS },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          MAX_TEXTURES },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, COMBINED_BINDINGS }
        },
        VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT
    );

    m_scene_set = vk::descriptors::allocate_set(m_scene_pool, m_scene_set_layout);

    sampler_create_info gtao_sampler_info;
    gtao_sampler_info.mag_filter = VK_FILTER_NEAREST;
    gtao_sampler_info.min_filter = VK_FILTER_NEAREST;
    gtao_sampler_info.mip_mode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    gtao_sampler_info.wrap_u     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    gtao_sampler_info.wrap_v     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    gtao_sampler_info.wrap_w     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    gtao_sampler_info.anisotropy = false;

    m_gtao_sampler = vk::samplers::get(vk::samplers::make(gtao_sampler_info));

    sampler_create_info linear_sampler_info;
    linear_sampler_info.mip_mode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    linear_sampler_info.wrap_u     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    linear_sampler_info.wrap_v     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    linear_sampler_info.wrap_w     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    linear_sampler_info.anisotropy = false;

    m_linear_sampler = vk::samplers::get(vk::samplers::make(linear_sampler_info));
}

void destroy_scene_set()
{
    vk::descriptors::destroy_pool(m_scene_pool);
    vk::descriptors::destroy_set_layout(m_scene_set_layout);

    m_scene_set_layout = VK_NULL_HANDLE;
    m_scene_pool       = VK_NULL_HANDLE;
    m_scene_set        = VK_NULL_HANDLE;

    m_bound_samplers = 0;

    m_written_gtao_view         = VK_NULL_HANDLE;
    m_written_shadow_view       = VK_NULL_HANDLE;
    m_written_cloud_shadow_view = VK_NULL_HANDLE;
    m_written_shadow_mask_view  = VK_NULL_HANDLE;
    m_written_fog_view          = VK_NULL_HANDLE;

    m_gtao_sampler   = VK_NULL_HANDLE;
    m_linear_sampler = VK_NULL_HANDLE;
}

void handle_scene_set()
{
    for (const texture* built : res::textures::take_built()) {
        vk::descriptors::write_image(
            m_scene_set,
            1,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            built->target.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_NULL_HANDLE,
            built->index
        );
    }

    for (; m_bound_samplers < vk::samplers::get_count(); m_bound_samplers++) {
        vk::descriptors::write_image(
            m_scene_set,
            0,
            VK_DESCRIPTOR_TYPE_SAMPLER,
            VK_NULL_HANDLE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            vk::samplers::get(m_bound_samplers),
            m_bound_samplers
        );
    }

    if (m_gtao_image.view != m_written_gtao_view) {
        vk::descriptors::write_image(
            m_scene_set,
            GTAO_BINDING,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            m_gtao_image.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            m_gtao_sampler
        );

        m_written_gtao_view = m_gtao_image.view;
    }

    const image& shadow_pool = shadows::get_pool_image();

    if (shadow_pool.view != m_written_shadow_view) {
        vk::descriptors::write_image(
            m_scene_set,
            SHADOW_BINDING,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            shadow_pool.view,
            VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
            m_gtao_sampler
        );

        m_written_shadow_view = shadow_pool.view;
    }

    const image& cloud_shadow = clouds::get_shadow_image();

    if (cloud_shadow.view != m_written_cloud_shadow_view) {
        vk::descriptors::write_image(
            m_scene_set,
            CLOUD_SHADOW_BINDING,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            cloud_shadow.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            m_linear_sampler
        );

        m_written_cloud_shadow_view = cloud_shadow.view;
    }

    const image& shadow_mask = shadows::get_mask_image();

    if (shadow_mask.view != m_written_shadow_mask_view) {
        vk::descriptors::write_image(
            m_scene_set,
            SHADOW_MASK_BINDING,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            shadow_mask.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            m_gtao_sampler
        );

        m_written_shadow_mask_view = shadow_mask.view;
    }

    const image& fog_volume = volumetric_fog::get_volume_image();

    if (fog_volume.view != m_written_fog_view) {
        vk::descriptors::write_image(
            m_scene_set,
            FOG_BINDING,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            fog_volume.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            m_linear_sampler
        );

        m_written_fog_view = fog_volume.view;
    }
}

void update_quality()
{
    const float budget = frame_pipe::get_post().gpu_budget_fraction * utilities::core::timer::target_frame_ms();
    const float gpu_ms = vk::profiler::get_timings().total_ms;

    if (gpu_ms > budget) {
        m_quality -= QUALITY_STEP_DOWN;
    } else if (gpu_ms < budget * QUALITY_RAISE_THRESHOLD) {
        m_quality += QUALITY_STEP_UP;
    }

    m_quality = std::clamp(m_quality, MIN_QUALITY, MAX_QUALITY);
}

glm::mat4 jitter_matrix()
{
    const VkExtent3D extent = m_depth_image.extent;

    m_jitter_offset = fsr::get_jitter_offset(m_frame_index, extent.width, m_display_extent.width);

    const float offset_x = m_jitter_offset.x * 2.0f / static_cast<float>(extent.width);
    const float offset_y = m_jitter_offset.y * 2.0f / static_cast<float>(extent.height);

    return glm::translate(glm::mat4(1.0f), glm::vec3(offset_x, offset_y, 0.0f));
}

void handle_frame_data()
{
    const gpu_camera& camera = frame_pipe::get_camera();

    const glm::mat4 jitter = jitter_matrix();

    frame_scene_data data;

    data.view          = camera.view;
    data.proj          = jitter * camera.proj;
    data.view_proj     = jitter * camera.view_proj;
    data.inv_view_proj = glm::inverse(data.view_proj);

    data.camera_pos = camera.position;
    data.timestamp  = utilities::core::timer::timestamp;

    data.light_count = frame_pipe::get_lights_count();

    data.has_environment       = frame_pipe::has_environment() ? 1 : 0;
    data.environment_intensity = frame_pipe::get_environment().intensity;

    data.quality = m_quality;

    data.objects   = frame_pipe::get_objects_address();
    data.materials = frame_pipe::get_materials_address();
    data.lights    = frame_pipe::get_lights_address();

    data.frame_index = m_frame_index;
    data.exposure    = exposure::get_exposure();

    data.environment_sh = frame_pipe::get_environment().irradiance_sh;
    data.atmosphere     = atmosphere::get_params_address();

    data.has_atmosphere = frame_pipe::has_atmosphere() ? 1 : 0;
    data.has_shadows    = shadows::is_active() ? 1 : 0;
    data.shadows        = shadows::get_params_address();
    data.fog            = volumetric_fog::get_params_address();
    data.has_fog        = volumetric_fog::is_active() ? 1 : 0;

    data.texture_lod_bias = fsr::get_texture_lod_bias(m_depth_image.extent.width, m_display_extent.width);

    data.unjittered_view_proj = camera.view_proj;
    data.previous_view_proj   = m_has_previous_view_proj ? m_previous_view_proj : camera.view_proj;

    m_previous_view_proj     = camera.view_proj;
    m_has_previous_view_proj = true;

    vk::resources::upload_buffer(m_frame_buffers[vk::frame::get_current_frame_index()], &data, sizeof(data));
}

}

void init()
{
    queue_deletor::push([]() { res::shaders::erase_all(); });
    queue_deletor::push([]() { res::meshes::erase_all(); });
    queue_deletor::push([]() { res::materials::erase_all(); });
    queue_deletor::push([]() { res::textures::erase_all(); });
    queue_deletor::push([]() { vk::samplers::destroy_all(); });
    queue_deletor::push([]() { res::models::erase_all(); });

    create_render_targets();

    create_frame_buffers();

    create_scene_set();

    environment::init_all();

    frame_pipe::init_all();

    atmosphere::init_all();
    clouds::init_all();

    gtao::init_all();
    picking::init_all();
    shadows::init_all();
    volumetric_fog::init_all();
    fsr::init_all();

    light_shafts::init_all();
    exposure::init_all();
    bloom::init_all();

#ifdef DEV_BUILD
    outline_mask::init_all();
#endif

    post_process::init_all();
}

void cleanup()
{
    post_process::clean_all();

#ifdef DEV_BUILD
    outline_mask::clean_all();
#endif

    bloom::clean_all();
    exposure::clean_all();
    light_shafts::clean_all();

    fsr::clean_all();
    volumetric_fog::clean_all();
    shadows::clean_all();
    picking::clean_all();
    gtao::clean_all();

    clouds::clean_all();
    atmosphere::clean_all();

    frame_pipe::clean_all();

    environment::clean_all();

    destroy_scene_set();

    destroy_frame_buffers();

    destroy_render_targets();
}

void handle_all()
{
    update_quality();

    res::models::erase_unused();

    res::meshes::destroy_erased();
    res::shaders::destroy_replaced();
    res::textures::destroy_erased();

    ensure_render_targets();

    shadows::handle_targets();

    handle_scene_set();

    clouds::handle_all();
    atmosphere::handle_all();

    frame_pipe::handle_all();

    shadows::handle_all();
    volumetric_fog::handle_all();

    gtao::handle_all();
    fsr::handle_all();

    light_shafts::handle_all();
    exposure::handle_all();
    bloom::handle_all();

#ifdef DEV_BUILD
    outline_mask::handle_all();
#endif

    post_process::handle_all();

    if (frame_pipe::has_camera()) {
        handle_frame_data();
    }

    m_frame_index++;
}

void draw_prepass()
{
    frame_pipe::draw_prepass();
}

void draw_all()
{
    frame_pipe::draw_all();
}

const image& get_depth_image()       { return m_depth_image; }
const image& get_hdr_image()         { return m_hdr_image; }
const image& get_normal_image()      { return m_normal_image; }
const image& get_velocity_image()    { return m_velocity_image; }
const image& get_gtao_image()        { return m_gtao_image; }
const image& get_output_image()      { return m_output_image; }

VkExtent2D get_render_extent() { return { m_depth_image.extent.width, m_depth_image.extent.height }; }

glm::vec2 get_jitter_offset() { return m_jitter_offset; }

VkDeviceAddress get_frame_data_address()
{
    return m_frame_buffers[vk::frame::get_current_frame_index()].address;
}

VkDescriptorSetLayout get_scene_set_layout() { return m_scene_set_layout; }
VkDescriptorSet get_scene_set()              { return m_scene_set; }

float get_quality() { return m_quality; }

}
