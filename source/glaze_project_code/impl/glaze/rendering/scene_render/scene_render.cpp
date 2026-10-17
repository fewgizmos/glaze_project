#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/atmosphere.h"
#include "glp/glaze/rendering/scene_render/bloom.h"
#include "glp/glaze/rendering/scene_render/clouds.h"
#include "glp/glaze/rendering/scene_render/exposure.h"
#include "glp/glaze/rendering/scene_render/light_shafts.h"
#include "glp/glaze/rendering/scene_render/environment.h"
#include "glp/glaze/rendering/scene_render/frame_pipeline.h"
#include "glp/glaze/rendering/scene_render/gtao.h"
#include "glp/glaze/rendering/scene_render/post_process.h"
#include "glp/glaze/rendering/scene_render/taa.h"

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

#include "glp/glaze/rendering/defines.h"
#include "glp/glaze/rendering/queue_deletor.h"

#include "glp/glaze/utilities/core/timer.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec3.hpp>

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
    uint32_t padding0;

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
    uint32_t padding1;
};

constexpr uint32_t GTAO_BINDING = 2;

constexpr uint32_t JITTER_SEQUENCE_LENGTH = 8;

image m_depth_image;
image m_hdr_image;
image m_normal_image;
image m_velocity_image;
image m_gtao_image;
image m_taa_output_image;
image m_taa_history_image;

buffer m_frame_buffers[MAX_FRAMES_IN_FLIGHT];

VkDescriptorSetLayout m_scene_set_layout = VK_NULL_HANDLE;
VkDescriptorPool m_scene_pool = VK_NULL_HANDLE;
VkDescriptorSet m_scene_set = VK_NULL_HANDLE;

uint32_t m_bound_samplers = 0;

VkImageView m_written_gtao_view = VK_NULL_HANDLE;
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
    const VkExtent2D extent = vk::frame::get_swapchain().extent;

    m_depth_image = vk::resources::create_image(
        { extent.width, extent.height, 1 },
        DEPTH_FORMAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    m_hdr_image      = create_target(extent, HDR_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    m_normal_image   = create_target(extent, GBUFFER_NORMAL_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    m_velocity_image = create_target(extent, GBUFFER_VELOCITY_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    m_gtao_image     = create_target(extent, GTAO_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    m_taa_output_image  = create_target(extent, HDR_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    m_taa_history_image = create_target(extent, HDR_FORMAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
}

void destroy_render_targets()
{
    vk::resources::destroy_image(m_taa_history_image);
    vk::resources::destroy_image(m_taa_output_image);
    vk::resources::destroy_image(m_gtao_image);
    vk::resources::destroy_image(m_velocity_image);
    vk::resources::destroy_image(m_normal_image);
    vk::resources::destroy_image(m_hdr_image);
    vk::resources::destroy_image(m_depth_image);
}

void ensure_render_targets()
{
    const VkExtent2D extent = vk::frame::get_swapchain().extent;

    if (m_depth_image.extent.width == extent.width && m_depth_image.extent.height == extent.height) {
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
            { 0,            VK_DESCRIPTOR_TYPE_SAMPLER,                MAX_SAMPLERS, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { 1,            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          MAX_TEXTURES, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { GTAO_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,            VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
        },
        { flags, flags, flags }
    );

    m_scene_pool = vk::descriptors::create_pool(
        1,
        {
            { VK_DESCRIPTOR_TYPE_SAMPLER,                MAX_SAMPLERS },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          MAX_TEXTURES },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
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
}

void destroy_scene_set()
{
    vk::descriptors::destroy_pool(m_scene_pool);
    vk::descriptors::destroy_set_layout(m_scene_set_layout);

    m_scene_set_layout = VK_NULL_HANDLE;
    m_scene_pool       = VK_NULL_HANDLE;
    m_scene_set        = VK_NULL_HANDLE;

    m_bound_samplers = 0;

    m_written_gtao_view = VK_NULL_HANDLE;
    m_gtao_sampler      = VK_NULL_HANDLE;
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

glm::mat4 jitter_matrix()
{
    if (!frame_pipe::get_post().taa_enabled) {
        return glm::mat4(1.0f);
    }

    const VkExtent2D extent = vk::frame::get_swapchain().extent;

    const uint32_t sample = m_frame_index % JITTER_SEQUENCE_LENGTH + 1;

    const float offset_x = (halton(sample, 2) - 0.5f) * 2.0f / static_cast<float>(extent.width);
    const float offset_y = (halton(sample, 3) - 0.5f) * 2.0f / static_cast<float>(extent.height);

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

    data.padding0 = 0;

    data.objects   = frame_pipe::get_objects_address();
    data.materials = frame_pipe::get_materials_address();
    data.lights    = frame_pipe::get_lights_address();

    data.frame_index = m_frame_index;
    data.exposure    = exposure::get_exposure();

    data.environment_sh = frame_pipe::get_environment().irradiance_sh;
    data.atmosphere     = atmosphere::get_params_address();

    data.has_atmosphere = frame_pipe::has_atmosphere() ? 1 : 0;
    data.padding1       = 0;

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
    taa::init_all();

    light_shafts::init_all();
    exposure::init_all();
    bloom::init_all();

    post_process::init_all();
}

void cleanup()
{
    post_process::clean_all();

    bloom::clean_all();
    exposure::clean_all();
    light_shafts::clean_all();

    taa::clean_all();
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
    res::models::erase_unused();

    res::meshes::destroy_erased();
    res::shaders::destroy_replaced();
    res::textures::destroy_erased();

    ensure_render_targets();

    handle_scene_set();

    clouds::handle_all();
    atmosphere::handle_all();

    frame_pipe::handle_all();

    gtao::handle_all();
    taa::handle_all();

    light_shafts::handle_all();
    exposure::handle_all();
    bloom::handle_all();

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
const image& get_taa_output_image()  { return m_taa_output_image; }
const image& get_taa_history_image() { return m_taa_history_image; }

VkDeviceAddress get_frame_data_address()
{
    return m_frame_buffers[vk::frame::get_current_frame_index()].address;
}

VkDescriptorSetLayout get_scene_set_layout() { return m_scene_set_layout; }
VkDescriptorSet get_scene_set()              { return m_scene_set; }

}
