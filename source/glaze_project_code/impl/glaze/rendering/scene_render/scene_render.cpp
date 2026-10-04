#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/debug_gizmos.h"

#include "glp/glaze/rendering/scene_render/resources/material.h"
#include "glp/glaze/rendering/scene_render/resources/mesh.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"
#include "glp/glaze/rendering/queue_deletor.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace glaze::rendering::scene_render
{

namespace
{

struct base_model_push
{
    glm::mat4 transform;
    VkDeviceAddress vertices;
};

constexpr uint32_t SPHERE_SEGMENTS = 32;
constexpr uint32_t SPHERE_RINGS    = 16;

constexpr float SPHERE_RADIUS = 1.0f;

const material* m_base_model_material = nullptr;

const mesh* m_sphere_mesh = nullptr;

image m_depth_image;

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
    base_model_create_info.push_constant_size = sizeof(base_model_push);
    base_model_create_info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT;

    m_base_model_material = resources::materials::build(
        "base_model",
        "base_model.vert",
        "base_model.frag",
        "",
        base_model_create_info,
        {}
    );
}

void draw_sphere()
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const glm::mat4 model = glm::mat4(1.0f);

    disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_base_model_material->graphics.handle);

    const base_model_push push = { get_view_proj() * model, m_sphere_mesh->vertex_buffer.address };

    vk::commands::push_constants(
        cmd,
        m_base_model_material->graphics.layout,
        VK_SHADER_STAGE_VERTEX_BIT,
        sizeof(push),
        &push
    );

    disp.cmdBindIndexBuffer(cmd, m_sphere_mesh->index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);

    disp.cmdDrawIndexed(cmd, m_sphere_mesh->index_count, 1, 0, 0, 0);
}

}

void init()
{
    queue_deletor::push([]() { resources::materials::erase_all(); });
    queue_deletor::push([]() { resources::meshes::erase_all(); });

    create_depth_image(vk::frame::get_swapchain().extent);

    create_sphere_mesh();

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

    m_sphere_mesh = nullptr;

    vk::resources::destroy_image(m_depth_image);

    m_base_model_material = nullptr;
}

void update_all()
{
    resources::meshes::destroy_erased();
    resources::materials::destroy_replaced();

    ensure_depth_image();
}

void draw_all()
{
    draw_sphere();

#ifndef NDEBUG
    debug_gizmos::draw_all();
#endif
}

const image& get_depth_image() { return m_depth_image; }

glm::mat4 get_view_proj()
{
    const VkExtent2D extent = vk::frame::get_swapchain().extent;

    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);

    const glm::mat4 view = glm::lookAt(glm::vec3(2.5f, 3.0f, 2.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 100.0f, 0.1f);

    proj[1][1] *= -1.0f;

    return proj * view;
}

}
