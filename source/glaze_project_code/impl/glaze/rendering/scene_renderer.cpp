#include "glp/glaze/rendering/scene_renderer.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/resources.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <vector>

namespace glaze::rendering::scene_renderer
{

namespace
{

struct cube_face
{
    glm::vec4 color;
    glm::uvec4 rng;
};

struct face_def
{
    glm::vec3 normal;
    glm::vec3 corners[4];
};

struct render_object
{
    const mesh* target;
    glm::mat4 model;
};

struct object_push
{
    glm::mat4 view_proj;
    glm::mat4 model;
};

constexpr uint32_t CUBE_FACE_COUNT = 6;

constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;

pipeline m_cube_pipeline;
// pipeline m_cube_colors_pipeline;

mesh m_cube_mesh;
std::vector<render_object> m_objects;

buffer m_cube_face_buffer;

image m_depth_image;

VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;
VkDescriptorSetLayout m_cube_set_layout = VK_NULL_HANDLE;
VkDescriptorSet m_cube_set = VK_NULL_HANDLE;

void create_depth_image(VkExtent2D extent)
{
    m_depth_image = vk::resources::create_image(
        { extent.width, extent.height, 1 },
        DEPTH_FORMAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );
}

void ensure_depth_image(VkExtent2D extent)
{
    if (m_depth_image.extent.width == extent.width && m_depth_image.extent.height == extent.height) {
        return;
    }

    vk::device::get_disp().deviceWaitIdle();

    vk::resources::destroy_image(m_depth_image);

    create_depth_image(extent);
}

void create_cube_mesh()
{
    const face_def faces[CUBE_FACE_COUNT] = {
        { {  0.0f,  0.0f,  1.0f }, { { -0.5f, -0.5f,  0.5f }, {  0.5f, -0.5f,  0.5f }, {  0.5f,  0.5f,  0.5f }, { -0.5f,  0.5f,  0.5f } } },
        { {  0.0f,  0.0f, -1.0f }, { {  0.5f, -0.5f, -0.5f }, { -0.5f, -0.5f, -0.5f }, { -0.5f,  0.5f, -0.5f }, {  0.5f,  0.5f, -0.5f } } },
        { {  1.0f,  0.0f,  0.0f }, { {  0.5f, -0.5f,  0.5f }, {  0.5f, -0.5f, -0.5f }, {  0.5f,  0.5f, -0.5f }, {  0.5f,  0.5f,  0.5f } } },
        { { -1.0f,  0.0f,  0.0f }, { { -0.5f, -0.5f, -0.5f }, { -0.5f, -0.5f,  0.5f }, { -0.5f,  0.5f,  0.5f }, { -0.5f,  0.5f, -0.5f } } },
        { {  0.0f,  1.0f,  0.0f }, { { -0.5f,  0.5f,  0.5f }, {  0.5f,  0.5f,  0.5f }, {  0.5f,  0.5f, -0.5f }, { -0.5f,  0.5f, -0.5f } } },
        { {  0.0f, -1.0f,  0.0f }, { { -0.5f, -0.5f, -0.5f }, {  0.5f, -0.5f, -0.5f }, {  0.5f, -0.5f,  0.5f }, { -0.5f, -0.5f,  0.5f } } }
    };

    const uint32_t face_indices[6] = { 0, 1, 2, 0, 2, 3 };

    std::vector<vertex> vertices;
    std::vector<uint32_t> indices;

    for (uint32_t f = 0; f < CUBE_FACE_COUNT; f++) {
        const uint32_t base = static_cast<uint32_t>(vertices.size());

        for (const glm::vec3& corner : faces[f].corners) {
            vertices.push_back({ corner, faces[f].normal, f });
        }

        for (const uint32_t index : face_indices) {
            indices.push_back(base + index);
        }
    }

    m_cube_mesh = vk::resources::create_mesh(vertices, indices);
}

void create_objects()
{
    m_objects.push_back({
        &m_cube_mesh,
        glm::translate(glm::mat4(1.0f), glm::vec3(-0.7f, 0.0f, 0.0f))
    });

    m_objects.push_back({
        &m_cube_mesh,
        glm::translate(glm::mat4(1.0f), glm::vec3(0.7f, 0.4f, -1.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.8f))
    });
}

void create_cube_resources()
{
    cube_face cube_faces[CUBE_FACE_COUNT];

    for (uint32_t i = 0; i < CUBE_FACE_COUNT; i++) {
        cube_faces[i] = { glm::vec4(1.0f), glm::uvec4(0x9E3779B9u * (i + 1), 0u, 0u, 0u) };
    }

    m_cube_face_buffer = vk::resources::create_buffer(
        sizeof(cube_faces),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    vk::resources::upload_buffer(m_cube_face_buffer, cube_faces, sizeof(cube_faces));

    m_descriptor_pool = vk::descriptors::create_pool(1, { { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 } });

    VkDescriptorSetLayoutBinding face_binding = {};
    face_binding.binding = 0;
    face_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    face_binding.descriptorCount = 1;
    face_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT;

    m_cube_set_layout = vk::descriptors::create_set_layout({ face_binding });

    m_cube_set = vk::descriptors::allocate_set(m_descriptor_pool, m_cube_set_layout);

    vk::descriptors::write_buffer(m_cube_set, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_cube_face_buffer);
}

void create_pipelines()
{
    // compute_pipeline_create_info cube_colors_create_info;
    // cube_colors_create_info.comp_file = "random_cube_colors.comp";
    // cube_colors_create_info.set_layouts = { m_cube_set_layout };

    // m_cube_colors_pipeline = vk::shaders::create_compute_pipeline(cube_colors_create_info);

    graphics_pipeline_create_info cube_create_info;
    cube_create_info.vert_file = "cube.vert";
    cube_create_info.frag_file = "cube.frag";
    cube_create_info.color_format = vk::frame::get_swapchain().image_format;
    cube_create_info.depth_format = DEPTH_FORMAT;
    cube_create_info.cull_mode = VK_CULL_MODE_BACK_BIT;
    cube_create_info.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    cube_create_info.set_layouts = { m_cube_set_layout };
    cube_create_info.push_constant_size = sizeof(object_push);
    cube_create_info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT;

    cube_create_info.vertex_bindings = {
        { 0, sizeof(vertex), VK_VERTEX_INPUT_RATE_VERTEX }
    };

    cube_create_info.vertex_attributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(vertex, pos)) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(vertex, normal)) },
        { 2, 0, VK_FORMAT_R32_UINT,         static_cast<uint32_t>(offsetof(vertex, face)) }
    };

    m_cube_pipeline = vk::shaders::create_graphics_pipeline(cube_create_info);
}

/*
void randomize_cube_colors(VkCommandBuffer cmd)
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cube_colors_pipeline.handle);

    disp.cmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_cube_colors_pipeline.layout,
        0,
        1,
        &m_cube_set,
        0,
        nullptr
    );

    disp.cmdDispatch(cmd, 1, 1, 1);

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT
    );
}
*/

void draw_objects(VkCommandBuffer cmd, const swapchain_image_data& target, VkExtent2D extent)
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    const glm::mat4 view = glm::lookAt(glm::vec3(1.5f, 1.2f, 2.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 proj = glm::perspectiveRH_ZO(
        glm::radians(60.0f),
        static_cast<float>(extent.width) / static_cast<float>(extent.height),
        100.0f,
        0.1f
    );

    proj[1][1] *= -1.0f;

    vk::commands::transition_image(
        cmd,
        target.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
    );

    vk::commands::transition_image(
        cmd,
        m_depth_image.handle,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    VkRenderingAttachmentInfo color_attachment = {};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = target.image_view;
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

    VkRenderingAttachmentInfo depth_attachment = {};
    depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_attachment.imageView = m_depth_image.view;
    depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.clearValue.depthStencil = { 0.0f, 0 };

    VkRenderingInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset = { 0, 0 };
    rendering_info.renderArea.extent = extent;
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;
    rendering_info.pDepthAttachment = &depth_attachment;

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;

    disp.cmdBeginRendering(cmd, &rendering_info);

    disp.cmdSetViewport(cmd, 0, 1, &viewport);
    disp.cmdSetScissor(cmd, 0, 1, &scissor);

    disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_cube_pipeline.handle);

    disp.cmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_cube_pipeline.layout,
        0,
        1,
        &m_cube_set,
        0,
        nullptr
    );

    for (const render_object& object : m_objects) {
        const object_push push = { proj * view, object.model };

        const VkDeviceSize offset = 0;

        vk::commands::push_constants(cmd, m_cube_pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(push), &push);

        disp.cmdBindVertexBuffers(cmd, 0, 1, &object.target->vertex_buffer.handle, &offset);
        disp.cmdBindIndexBuffer(cmd, object.target->index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);

        disp.cmdDrawIndexed(cmd, object.target->index_count, 1, 0, 0, 0);
    }

    disp.cmdEndRendering(cmd);
}

}

void init()
{
    create_depth_image(vk::frame::get_swapchain().extent);

    create_cube_mesh();
    create_objects();

    create_cube_resources();
    create_pipelines();
}

void cleanup()
{
    vk::shaders::destroy_pipeline(m_cube_pipeline);
    // vk::shaders::destroy_pipeline(m_cube_colors_pipeline);

    vk::descriptors::destroy_set_layout(m_cube_set_layout);
    vk::descriptors::destroy_pool(m_descriptor_pool);

    vk::resources::destroy_buffer(m_cube_face_buffer);

    vk::resources::destroy_mesh(m_cube_mesh);
    m_objects.clear();

    vk::resources::destroy_image(m_depth_image);

    m_cube_pipeline = {};
    // m_cube_colors_pipeline = {};

    m_descriptor_pool = VK_NULL_HANDLE;
    m_cube_set_layout = VK_NULL_HANDLE;
    m_cube_set = VK_NULL_HANDLE;
}

void draw_all(VkCommandBuffer cmd, const swapchain_image_data& target, VkExtent2D extent)
{
    ensure_depth_image(extent);

    // randomize_cube_colors(cmd);
    draw_objects(cmd, target, extent);
}

}
