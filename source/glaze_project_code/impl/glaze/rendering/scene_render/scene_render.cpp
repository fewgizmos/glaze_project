#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/debug_grid.h"
#include "glp/glaze/rendering/scene_render/objects_pipeline.h"

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

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
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

    glm::vec3 light_dir;
    float padding;

    VkDeviceAddress objects;
    VkDeviceAddress materials;
};

const glm::vec3 CAMERA_POS    = glm::vec3(2.5f, 3.0f, 2.5f);
const glm::vec3 CAMERA_TARGET = glm::vec3(0.0f);

const glm::vec3 LIGHT_DIR = glm::vec3(-0.8f, 0.6f, 0.3f);

image m_depth_image;

buffer m_frame_buffers[MAX_FRAMES_IN_FLIGHT];

VkDescriptorSetLayout m_scene_set_layout = VK_NULL_HANDLE;
VkDescriptorPool m_scene_pool = VK_NULL_HANDLE;
VkDescriptorSet m_scene_set = VK_NULL_HANDLE;

uint32_t m_bound_samplers = 0;

void create_depth_image(VkExtent2D extent)
{
    m_depth_image = vk::resources::create_image(
        { extent.width, extent.height, 1 },
        DEPTH_FORMAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );
}

void ensure_depth_image()
{
    const VkExtent2D extent = vk::frame::get_swapchain().extent;

    if (m_depth_image.extent.width == extent.width && m_depth_image.extent.height == extent.height) {
        return;
    }

    vk::device::get_disp().deviceWaitIdle();

    vk::resources::destroy_image(m_depth_image);

    create_depth_image(extent);
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
    for (const texture* built : resources::textures::take_built()) {
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
    const VkExtent2D extent = vk::frame::get_swapchain().extent;

    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);

    frame_scene_data data;

    data.view = glm::lookAt(CAMERA_POS, CAMERA_TARGET, glm::vec3(0.0f, 1.0f, 0.0f));
    data.proj = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 100.0f, 0.1f);

    data.proj[1][1] *= -1.0f;

    data.view_proj     = data.proj * data.view;
    data.inv_view_proj = glm::inverse(data.view_proj);

    data.camera_pos = CAMERA_POS;
    data.timestamp  = utilities::core::timer::timestamp;

    data.light_dir = glm::normalize(LIGHT_DIR);
    data.padding   = 0.0f;

    data.objects   = objects_pipeline::get_objects_address();
    data.materials = objects_pipeline::get_materials_address();

    vk::resources::upload_buffer(m_frame_buffers[vk::frame::get_current_frame_index()], &data, sizeof(data));
}

}

void init()
{
    queue_deletor::push([]() { resources::shaders::erase_all(); });
    queue_deletor::push([]() { resources::meshes::erase_all(); });
    queue_deletor::push([]() { resources::materials::erase_all(); });
    queue_deletor::push([]() { resources::textures::erase_all(); });
    queue_deletor::push([]() { vk::samplers::destroy_all(); });
    queue_deletor::push([]() { resources::models::erase_all(); });

    create_depth_image(vk::frame::get_swapchain().extent);

    create_frame_buffers();

    create_scene_set();

    objects_pipeline::init_all();

#ifndef NDEBUG
    debug_grid::init();
#endif
}

void cleanup()
{
#ifndef NDEBUG
    debug_grid::clean();
#endif

    objects_pipeline::clean_all();

    destroy_scene_set();

    destroy_frame_buffers();

    vk::resources::destroy_image(m_depth_image);
}

void handle_all()
{
    resources::models::erase_unused();

    resources::meshes::destroy_erased();
    resources::shaders::destroy_replaced();
    resources::textures::destroy_erased();

    ensure_depth_image();

    handle_scene_set();

    objects_pipeline::handle_all();

    handle_frame_data();
}

void draw_all()
{
    objects_pipeline::draw_all();

#ifndef NDEBUG
    debug_grid::draw();
#endif
}

const image& get_depth_image() { return m_depth_image; }

VkDeviceAddress get_frame_data_address()
{
    return m_frame_buffers[vk::frame::get_current_frame_index()].address;
}

VkDescriptorSetLayout get_scene_set_layout() { return m_scene_set_layout; }
VkDescriptorSet get_scene_set()              { return m_scene_set; }

}
