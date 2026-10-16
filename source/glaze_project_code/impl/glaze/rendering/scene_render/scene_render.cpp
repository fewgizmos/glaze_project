#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/environment.h"
#include "glp/glaze/rendering/scene_render/frame_pipeline.h"
#include "glp/glaze/rendering/scene_render/post_process.h"

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

#include <glm/mat4x4.hpp>
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
    uint32_t padding;

    VkDeviceAddress objects;
    VkDeviceAddress materials;
    VkDeviceAddress lights;
};

image m_depth_image;
image m_hdr_image;

buffer m_frame_buffers[MAX_FRAMES_IN_FLIGHT];

VkDescriptorSetLayout m_scene_set_layout = VK_NULL_HANDLE;
VkDescriptorPool m_scene_pool = VK_NULL_HANDLE;
VkDescriptorSet m_scene_set = VK_NULL_HANDLE;

uint32_t m_bound_samplers = 0;

void create_render_targets()
{
    const VkExtent2D extent = vk::frame::get_swapchain().extent;

    m_depth_image = vk::resources::create_image(
        { extent.width, extent.height, 1 },
        DEPTH_FORMAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    m_hdr_image = vk::resources::create_image(
        { extent.width, extent.height, 1 },
        HDR_FORMAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
}

void destroy_render_targets()
{
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
            { 0, VK_DESCRIPTOR_TYPE_SAMPLER,       MAX_SAMPLERS, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_TEXTURES, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
        },
        { flags, flags }
    );

    m_scene_pool = vk::descriptors::create_pool(
        1,
        {
            { VK_DESCRIPTOR_TYPE_SAMPLER,       MAX_SAMPLERS },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_TEXTURES }
        },
        VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT
    );

    m_scene_set = vk::descriptors::allocate_set(m_scene_pool, m_scene_set_layout);
}

void destroy_scene_set()
{
    vk::descriptors::destroy_pool(m_scene_pool);
    vk::descriptors::destroy_set_layout(m_scene_set_layout);

    m_scene_set_layout = VK_NULL_HANDLE;
    m_scene_pool       = VK_NULL_HANDLE;
    m_scene_set        = VK_NULL_HANDLE;

    m_bound_samplers = 0;
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
}

void handle_frame_data()
{
    const gpu_camera& camera = frame_pipe::get_camera();

    frame_scene_data data;

    data.view          = camera.view;
    data.proj          = camera.proj;
    data.view_proj     = camera.view_proj;
    data.inv_view_proj = camera.inv_view_proj;

    data.camera_pos = camera.position;
    data.timestamp  = utilities::core::timer::timestamp;

    data.light_count = frame_pipe::get_lights_count();

    data.has_environment       = frame_pipe::has_environment() ? 1 : 0;
    data.environment_intensity = frame_pipe::get_environment().intensity;

    data.padding = 0;

    data.objects   = frame_pipe::get_objects_address();
    data.materials = frame_pipe::get_materials_address();
    data.lights    = frame_pipe::get_lights_address();

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

    post_process::init_all();
}

void cleanup()
{
    post_process::clean_all();

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

    frame_pipe::handle_all();

    post_process::handle_all();

    if (frame_pipe::has_camera()) {
        handle_frame_data();
    }
}

void draw_all()
{
    frame_pipe::draw_all();
}

const image& get_depth_image() { return m_depth_image; }
const image& get_hdr_image()   { return m_hdr_image; }

VkDeviceAddress get_frame_data_address()
{
    return m_frame_buffers[vk::frame::get_current_frame_index()].address;
}

VkDescriptorSetLayout get_scene_set_layout() { return m_scene_set_layout; }
VkDescriptorSet get_scene_set()              { return m_scene_set; }

}
