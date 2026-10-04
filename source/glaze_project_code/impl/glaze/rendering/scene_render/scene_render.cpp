#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/debug_gizmos.h"

#include "glp/glaze/rendering/scene_render/resources/material.h"
#include "glp/glaze/rendering/scene_render/resources/mesh.h"
#include "glp/glaze/rendering/scene_render/resources/texture.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"
#include "glp/glaze/rendering/queue_deletor.h"

#include "glp/glaze/utilities/core/timer.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace glaze::rendering::scene_render
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
};

struct object_data
{
    glm::mat4 model;
    VkDeviceAddress vertices;
    uint32_t vertex_count;
    uint32_t texture_index;
    uint32_t sampler_index;
    uint32_t padding[3];
};

constexpr uint32_t NO_TEXTURE = 0xFFFFFFFF;

constexpr const char* SPHERE_TEXTURE_PATH = "source/glaze_project_resources/textures/some_texture.png";

constexpr uint32_t SPHERE_SEGMENTS = 512;
constexpr uint32_t SPHERE_RINGS    = 256;

constexpr float SPHERE_RADIUS = 1.0f;

constexpr uint32_t COMPUTE_GROUP_SIZE = 64;

constexpr uint32_t OBJECTS_START_CAPACITY = 64;

const glm::vec3 CAMERA_POS    = glm::vec3(2.5f, 3.0f, 2.5f);
const glm::vec3 CAMERA_TARGET = glm::vec3(0.0f);

const glm::vec3 LIGHT_DIR = glm::vec3(-0.8f, 0.6f, 0.3f);

const material* m_base_model_material = nullptr;

const mesh* m_sphere_mesh = nullptr;

const texture* m_sphere_texture = nullptr;

uint32_t m_sphere_sampler = 0;
uint32_t m_sphere_object  = 0;

image m_depth_image;

VkDescriptorSetLayout m_scene_set_layout = VK_NULL_HANDLE;
VkDescriptorPool m_scene_pool = VK_NULL_HANDLE;
VkDescriptorSet m_scene_set = VK_NULL_HANDLE;

uint32_t m_bound_samplers = 0;

buffer m_frame_buffers[MAX_FRAMES_IN_FLIGHT];
buffer m_object_buffers[MAX_FRAMES_IN_FLIGHT];

std::vector<object_data> m_objects;

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

buffer create_shader_buffer(VkDeviceSize size)
{
    return vk::resources::create_buffer(
        size,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
}

void create_frame_buffers()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_frame_buffers[i]  = create_shader_buffer(sizeof(frame_scene_data));
        m_object_buffers[i] = create_shader_buffer(sizeof(object_data) * OBJECTS_START_CAPACITY);
    }
}

void destroy_frame_buffers()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::resources::destroy_buffer(m_object_buffers[i]);
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

void create_sphere_texture()
{
    m_sphere_texture = resources::textures::build_stb(SPHERE_TEXTURE_PATH, true, 0);

    m_sphere_sampler = vk::samplers::make({});
}

void create_sphere_mesh()
{
    std::vector<vertex> vertices;
    std::vector<uint32_t> indices;

    vertices.reserve((SPHERE_RINGS + 1) * (SPHERE_SEGMENTS + 1));
    indices.reserve(SPHERE_RINGS * SPHERE_SEGMENTS * 6);

    for (uint32_t ring = 0; ring <= SPHERE_RINGS; ring++) {
        const float v     = static_cast<float>(ring) / static_cast<float>(SPHERE_RINGS);
        const float theta = v * glm::pi<float>();

        for (uint32_t segment = 0; segment <= SPHERE_SEGMENTS; segment++) {
            const float u   = static_cast<float>(segment) / static_cast<float>(SPHERE_SEGMENTS);
            const float phi = u * 2.0f * glm::pi<float>();

            const glm::vec3 normal(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));

            vertex point;
            point.pos     = normal * SPHERE_RADIUS;
            point.uv_x    = u;
            point.normal  = normal;
            point.uv_y    = v;
            point.tangent = glm::vec4(-std::sin(phi), 0.0f, std::cos(phi), 1.0f);
            point.color   = glm::vec4(1.0f);

            vertices.push_back(point);
        }
    }

    for (uint32_t ring = 0; ring < SPHERE_RINGS; ring++) {
        for (uint32_t segment = 0; segment < SPHERE_SEGMENTS; segment++) {
            const uint32_t current = ring * (SPHERE_SEGMENTS + 1) + segment;
            const uint32_t below   = current + SPHERE_SEGMENTS + 1;

            indices.push_back(current);
            indices.push_back(current + 1);
            indices.push_back(below);

            indices.push_back(current + 1);
            indices.push_back(below + 1);
            indices.push_back(below);
        }
    }

    m_sphere_mesh = resources::meshes::build(vertices, indices);
}

void create_pipelines()
{
    graphics_pipeline_create_info base_model_create_info;
    base_model_create_info.color_format = vk::frame::get_swapchain().image_format;
    base_model_create_info.depth_format = DEPTH_FORMAT;
    base_model_create_info.cull_mode = VK_CULL_MODE_BACK_BIT;
    base_model_create_info.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    base_model_create_info.set_layouts = { m_scene_set_layout };
    base_model_create_info.push_constant_size = sizeof(VkDeviceAddress);
    base_model_create_info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    compute_pipeline_create_info base_model_compute_create_info;
    base_model_compute_create_info.push_constant_size = sizeof(VkDeviceAddress);

    m_base_model_material = resources::materials::build(
        "base_model",
        "base_model.vert",
        "base_model.frag",
        "relief.comp",
        base_model_create_info,
        base_model_compute_create_info
    );
}

uint32_t add_object(const glm::mat4& model, const mesh* target, const texture* surface, uint32_t sampler_index)
{
    object_data object = {};

    object.model         = model;
    object.vertices      = target->vertex_buffer.address;
    object.vertex_count  = target->vertex_count;
    object.texture_index = surface != nullptr ? surface->index : NO_TEXTURE;
    object.sampler_index = sampler_index;

    m_objects.push_back(object);

    return static_cast<uint32_t>(m_objects.size() - 1);
}

void handle_objects()
{
    m_objects.clear();

    m_sphere_object = add_object(glm::mat4(1.0f), m_sphere_mesh, m_sphere_texture, m_sphere_sampler);

    buffer& target = m_object_buffers[vk::frame::get_current_frame_index()];

    const VkDeviceSize size = sizeof(object_data) * m_objects.size();

    if (size > target.size) {
        VkDeviceSize grown = target.size;

        while (grown < size) {
            grown *= 2;
        }

        vk::resources::destroy_buffer(target);

        target = create_shader_buffer(grown);
    }

    vk::resources::upload_buffer(target, m_objects.data(), size);
}

void handle_frame_data()
{
    const uint32_t frame_index = vk::frame::get_current_frame_index();

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

    data.objects = m_object_buffers[frame_index].address;

    vk::resources::upload_buffer(m_frame_buffers[frame_index], &data, sizeof(data));
}

void handle_compute()
{
    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    uint32_t max_vertex_count = 0;

    for (const object_data& object : m_objects) {
        max_vertex_count = std::max(max_vertex_count, object.vertex_count);
    }

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    const VkDeviceAddress push = get_frame_data_address();

    vk::commands::dispatch(
        cmd,
        m_base_model_material->compute,
        &push,
        sizeof(push),
        (max_vertex_count + COMPUTE_GROUP_SIZE - 1) / COMPUTE_GROUP_SIZE,
        static_cast<uint32_t>(m_objects.size()),
        1
    );

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT
    );
}

void draw_sphere()
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_base_model_material->graphics.handle);

    const VkDeviceAddress push = get_frame_data_address();

    vk::commands::push_constants(
        cmd,
        m_base_model_material->graphics.layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        sizeof(push),
        &push
    );

    disp.cmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_base_model_material->graphics.layout,
        0,
        1,
        &m_scene_set,
        0,
        nullptr
    );

    disp.cmdBindIndexBuffer(cmd, m_sphere_mesh->index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);

    disp.cmdDrawIndexed(cmd, m_sphere_mesh->index_count, 1, 0, 0, m_sphere_object);
}

}

void init()
{
    queue_deletor::push([]() { resources::materials::erase_all(); });
    queue_deletor::push([]() { resources::meshes::erase_all(); });
    queue_deletor::push([]() { resources::textures::erase_all(); });
    queue_deletor::push([]() { vk::samplers::destroy_all(); });

    create_depth_image(vk::frame::get_swapchain().extent);

    create_frame_buffers();

    create_scene_set();

    create_sphere_mesh();
    create_sphere_texture();

    create_pipelines();

#ifndef NDEBUG
    debug_gizmos::init_all();
#endif
}

void cleanup()
{
#ifndef NDEBUG
    debug_gizmos::clean_all();
#endif

    resources::meshes::erase(m_sphere_mesh);
    resources::textures::erase(m_sphere_texture);

    m_sphere_mesh    = nullptr;
    m_sphere_texture = nullptr;

    destroy_scene_set();

    destroy_frame_buffers();

    vk::resources::destroy_image(m_depth_image);

    m_base_model_material = nullptr;
}

void handle_all()
{
    resources::meshes::destroy_erased();
    resources::materials::destroy_replaced();
    resources::textures::destroy_erased();

    ensure_depth_image();

    handle_scene_set();

    handle_objects();
    handle_frame_data();

    handle_compute();
}

void draw_all()
{
    draw_sphere();

#ifndef NDEBUG
    debug_gizmos::draw_all();
#endif
}

const image& get_depth_image() { return m_depth_image; }

VkDeviceAddress get_frame_data_address()
{
    return m_frame_buffers[vk::frame::get_current_frame_index()].address;
}

}
