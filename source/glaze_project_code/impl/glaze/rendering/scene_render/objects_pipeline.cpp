#include "glp/glaze/rendering/scene_render/objects_pipeline.h"

#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/materials.h"
#include "glp/glaze/rendering/scene_render/resources/meshes.h"
#include "glp/glaze/rendering/scene_render/resources/shaders.h"
#include "glp/glaze/rendering/scene_render/resources/textures.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"

#include <glm/ext/scalar_constants.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace glaze::rendering::scene_render::objects_pipeline
{

namespace
{

struct render_object
{
    const mesh* target_mesh = nullptr;
    const material* target_material = nullptr;

    glm::mat4 model = glm::mat4(1.0f);
};

struct object_data
{
    glm::mat4 model;
    VkDeviceAddress vertices;
    uint32_t vertex_count;
    uint32_t material_index;
};

constexpr uint32_t SPHERE_SEGMENTS = 512;
constexpr uint32_t SPHERE_RINGS    = 256;

constexpr float SPHERE_RADIUS = 1.0f;

constexpr const char* SPHERE_TEXTURE_PATH = "source/glaze_project_resources/textures/some_texture.png";

constexpr uint32_t COMPUTE_GROUP_SIZE = 64;

constexpr uint32_t OBJECTS_START_CAPACITY   = 64;
constexpr uint32_t MATERIALS_START_CAPACITY = 64;

const shader* m_base_model_shader = nullptr;

const mesh* m_sphere_mesh = nullptr;
const texture* m_sphere_texture = nullptr;
const material* m_sphere_material = nullptr;

std::vector<render_object> m_objects;

std::vector<object_data> m_objects_data;

buffer m_object_buffers[MAX_FRAMES_IN_FLIGHT];
buffer m_material_buffers[MAX_FRAMES_IN_FLIGHT];

buffer create_shader_buffer(VkDeviceSize size)
{
    return vk::resources::create_buffer(
        size,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
}

void create_buffers()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_object_buffers[i]   = create_shader_buffer(sizeof(object_data) * OBJECTS_START_CAPACITY);
        m_material_buffers[i] = create_shader_buffer(sizeof(material_data) * MATERIALS_START_CAPACITY);
    }
}

void destroy_buffers()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::resources::destroy_buffer(m_material_buffers[i]);
        vk::resources::destroy_buffer(m_object_buffers[i]);
    }
}

void upload_grown(buffer& target, const void* data, VkDeviceSize size)
{
    if (size == 0) {
        return;
    }

    if (size > target.size) {
        VkDeviceSize grown = target.size;

        while (grown < size) {
            grown *= 2;
        }

        vk::resources::destroy_buffer(target);

        target = create_shader_buffer(grown);
    }

    vk::resources::upload_buffer(target, data, size);
}

void create_shaders()
{
    graphics_pipeline_create_info base_model_create_info;
    base_model_create_info.color_format = vk::frame::get_swapchain().image_format;
    base_model_create_info.depth_format = DEPTH_FORMAT;
    base_model_create_info.cull_mode = VK_CULL_MODE_BACK_BIT;
    base_model_create_info.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    base_model_create_info.set_layouts = { scene_render::get_scene_set_layout() };
    base_model_create_info.push_constant_size = sizeof(VkDeviceAddress);
    base_model_create_info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    compute_pipeline_create_info base_model_compute_create_info;
    base_model_compute_create_info.push_constant_size = sizeof(VkDeviceAddress);

    m_base_model_shader = resources::shaders::build(
        "base_model",
        "base_model.vert",
        "base_model.frag",
        "relief.comp",
        base_model_create_info,
        base_model_compute_create_info
    );
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

void create_sphere()
{
    create_sphere_mesh();

    m_sphere_texture = resources::textures::build_stb(SPHERE_TEXTURE_PATH, true, 0);

    material_create_info sphere_material_create_info;
    sphere_material_create_info.target_shader = m_base_model_shader;
    sphere_material_create_info.base_color_texture = { m_sphere_texture, vk::samplers::make({}) };

    m_sphere_material = resources::materials::build(sphere_material_create_info);

    m_objects.push_back({ m_sphere_mesh, m_sphere_material, glm::mat4(1.0f) });
}

void destroy_sphere()
{
    m_objects.clear();

    resources::materials::erase(m_sphere_material);
    resources::textures::erase(m_sphere_texture);
    resources::meshes::erase(m_sphere_mesh);

    m_sphere_material = nullptr;
    m_sphere_texture  = nullptr;
    m_sphere_mesh     = nullptr;
}

void handle_buffers()
{
    const uint32_t frame_index = vk::frame::get_current_frame_index();

    m_objects_data.clear();

    for (const render_object& target : m_objects) {
        m_objects_data.push_back({
            target.model,
            target.target_mesh->vertex_buffer.address,
            target.target_mesh->vertex_count,
            target.target_material->index
        });
    }

    const std::vector<material_data>& materials_data = resources::materials::get_data();

    upload_grown(m_object_buffers[frame_index], m_objects_data.data(), sizeof(object_data) * m_objects_data.size());

    upload_grown(
        m_material_buffers[frame_index],
        materials_data.data(),
        sizeof(material_data) * materials_data.size()
    );
}

void handle_compute()
{
    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    uint32_t max_vertex_count = 0;

    for (const object_data& data : m_objects_data) {
        max_vertex_count = std::max(max_vertex_count, data.vertex_count);
    }

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    const VkDeviceAddress push = scene_render::get_frame_data_address();

    vk::commands::dispatch(
        cmd,
        m_base_model_shader->compute,
        &push,
        sizeof(push),
        (max_vertex_count + COMPUTE_GROUP_SIZE - 1) / COMPUTE_GROUP_SIZE,
        static_cast<uint32_t>(m_objects_data.size()),
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

void bind_shader(const shader* target)
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const VkDescriptorSet scene_set = scene_render::get_scene_set();

    const VkDeviceAddress push = scene_render::get_frame_data_address();

    disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, target->graphics.handle);

    vk::commands::push_constants(
        cmd,
        target->graphics.layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        sizeof(push),
        &push
    );

    disp.cmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        target->graphics.layout,
        0,
        1,
        &scene_set,
        0,
        nullptr
    );
}

}

void init_all()
{
    create_buffers();

    create_shaders();

    create_sphere();
}

void clean_all()
{
    destroy_sphere();

    destroy_buffers();

    m_objects_data.clear();

    m_base_model_shader = nullptr;
}

void handle_all()
{
    handle_buffers();

    handle_compute();
}

void draw_all()
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const shader* bound_shader = nullptr;

    for (uint32_t i = 0; i < m_objects.size(); i++) {
        const render_object& target = m_objects[i];

        if (target.target_material->target_shader != bound_shader) {
            bound_shader = target.target_material->target_shader;

            bind_shader(bound_shader);
        }

        disp.cmdBindIndexBuffer(cmd, target.target_mesh->index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);

        disp.cmdDrawIndexed(cmd, target.target_mesh->index_count, 1, 0, 0, i);
    }
}

VkDeviceAddress get_objects_address()
{
    return m_object_buffers[vk::frame::get_current_frame_index()].address;
}

VkDeviceAddress get_materials_address()
{
    return m_material_buffers[vk::frame::get_current_frame_index()].address;
}

}
