#include "glp/glaze/rendering/scene_renderer.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/resources.h"
#include "glp/glaze/rendering/vk/shaders.h"

// #include "glp/glaze/utilities/core/timer.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace glaze::rendering::scene_renderer
{

namespace
{

struct cube_face
{
    glm::vec4 color;
    glm::uvec4 rng;
};

struct cube_push
{
    glm::mat4 mvp;
};

constexpr uint32_t CUBE_VERTEX_COUNT = 36;
constexpr uint32_t CUBE_FACE_COUNT   = 6;

pipeline m_cube_pipeline;
// pipeline m_cube_colors_pipeline;

buffer m_cube_pos_buffer;
buffer m_cube_face_buffer;
buffer m_cube_normal_buffer;

VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;
VkDescriptorSetLayout m_cube_set_layout = VK_NULL_HANDLE;
VkDescriptorSet m_cube_set = VK_NULL_HANDLE;

// float m_cube_rot = 0.0f;

void create_cube_resources()
{
    const glm::vec4 cube_pos[CUBE_VERTEX_COUNT] = {
        { -0.5f, -0.5f,  0.5f, 1.0f }, {  0.5f, -0.5f,  0.5f, 1.0f }, {  0.5f,  0.5f,  0.5f, 1.0f },
        { -0.5f, -0.5f,  0.5f, 1.0f }, {  0.5f,  0.5f,  0.5f, 1.0f }, { -0.5f,  0.5f,  0.5f, 1.0f },

        {  0.5f, -0.5f, -0.5f, 1.0f }, { -0.5f, -0.5f, -0.5f, 1.0f }, { -0.5f,  0.5f, -0.5f, 1.0f },
        {  0.5f, -0.5f, -0.5f, 1.0f }, { -0.5f,  0.5f, -0.5f, 1.0f }, {  0.5f,  0.5f, -0.5f, 1.0f },

        {  0.5f, -0.5f,  0.5f, 1.0f }, {  0.5f, -0.5f, -0.5f, 1.0f }, {  0.5f,  0.5f, -0.5f, 1.0f },
        {  0.5f, -0.5f,  0.5f, 1.0f }, {  0.5f,  0.5f, -0.5f, 1.0f }, {  0.5f,  0.5f,  0.5f, 1.0f },

        { -0.5f, -0.5f, -0.5f, 1.0f }, { -0.5f, -0.5f,  0.5f, 1.0f }, { -0.5f,  0.5f,  0.5f, 1.0f },
        { -0.5f, -0.5f, -0.5f, 1.0f }, { -0.5f,  0.5f,  0.5f, 1.0f }, { -0.5f,  0.5f, -0.5f, 1.0f },

        { -0.5f,  0.5f,  0.5f, 1.0f }, {  0.5f,  0.5f,  0.5f, 1.0f }, {  0.5f,  0.5f, -0.5f, 1.0f },
        { -0.5f,  0.5f,  0.5f, 1.0f }, {  0.5f,  0.5f, -0.5f, 1.0f }, { -0.5f,  0.5f, -0.5f, 1.0f },

        { -0.5f, -0.5f, -0.5f, 1.0f }, {  0.5f, -0.5f, -0.5f, 1.0f }, {  0.5f, -0.5f,  0.5f, 1.0f },
        { -0.5f, -0.5f, -0.5f, 1.0f }, {  0.5f, -0.5f,  0.5f, 1.0f }, { -0.5f, -0.5f,  0.5f, 1.0f }
    };

    cube_face cube_faces[CUBE_FACE_COUNT];

    for (uint32_t i = 0; i < CUBE_FACE_COUNT; i++) {
        cube_faces[i] = { glm::vec4(1.0f), glm::uvec4(0x9E3779B9u * (i + 1), 0u, 0u, 0u) };
    }

    const glm::vec4 cube_normals[CUBE_FACE_COUNT] = {
        {  0.0f,  0.0f,  1.0f, 0.0f },
        {  0.0f,  0.0f, -1.0f, 0.0f },
        {  1.0f,  0.0f,  0.0f, 0.0f },
        { -1.0f,  0.0f,  0.0f, 0.0f },
        {  0.0f,  1.0f,  0.0f, 0.0f },
        {  0.0f, -1.0f,  0.0f, 0.0f }
    };

    m_cube_pos_buffer = vk::resources::create_buffer(
        sizeof(cube_pos),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    m_cube_face_buffer = vk::resources::create_buffer(
        sizeof(cube_faces),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    vk::resources::upload_buffer(m_cube_pos_buffer, cube_pos, sizeof(cube_pos));
    vk::resources::upload_buffer(m_cube_face_buffer, cube_faces, sizeof(cube_faces));

    m_cube_normal_buffer = vk::resources::create_buffer(
        sizeof(cube_normals),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    vk::resources::upload_buffer(m_cube_normal_buffer, cube_normals, sizeof(cube_normals));

    m_descriptor_pool = vk::descriptors::create_pool(1, { { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 } });

    VkDescriptorSetLayoutBinding pos_binding = {};
    pos_binding.binding = 0;
    pos_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pos_binding.descriptorCount = 1;
    pos_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding face_binding = {};
    face_binding.binding = 1;
    face_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    face_binding.descriptorCount = 1;
    face_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding normal_binding = {};
    normal_binding.binding = 2;
    normal_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    normal_binding.descriptorCount = 1;
    normal_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    m_cube_set_layout = vk::descriptors::create_set_layout({ pos_binding, face_binding, normal_binding });

    m_cube_set = vk::descriptors::allocate_set(m_descriptor_pool, m_cube_set_layout);

    vk::descriptors::write_buffer(m_cube_set, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_cube_pos_buffer);
    vk::descriptors::write_buffer(m_cube_set, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_cube_face_buffer);
    vk::descriptors::write_buffer(m_cube_set, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_cube_normal_buffer);
}

void create_pipelines()
{
    // compute_pipeline_info cube_colors_info;
    // cube_colors_info.comp_file = "random_cube_colors.comp";
    // cube_colors_info.set_layouts = { m_cube_set_layout };

    // m_cube_colors_pipeline = vk::shaders::create_compute_pipeline(cube_colors_info);

    graphics_pipeline_info cube_info;
    cube_info.vert_file = "cube.vert";
    cube_info.frag_file = "cube.frag";
    cube_info.color_format = vk::frame::get_swapchain().image_format;
    cube_info.cull_mode = VK_CULL_MODE_BACK_BIT;
    cube_info.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    cube_info.set_layouts = { m_cube_set_layout };
    cube_info.push_constant_size = sizeof(cube_push);
    cube_info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT;

    m_cube_pipeline = vk::shaders::create_graphics_pipeline(cube_info);
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

void draw_cube(VkCommandBuffer cmd, const swapchain_image_data& target, VkExtent2D extent)
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    // m_cube_rot += utilities::core::timer::delta;
    // const glm::mat4 model = glm::rotate(glm::mat4(1.0f), m_cube_rot, glm::vec3(0.4f, 1.0f, 0.2f));

    const glm::mat4 view = glm::lookAt(glm::vec3(1.5f, 1.2f, 2.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 proj = glm::perspectiveRH_ZO(
        glm::radians(60.0f),
        static_cast<float>(extent.width) / static_cast<float>(extent.height),
        0.1f,
        100.0f
    );

    proj[1][1] *= -1.0f;

    // const cube_push push = { proj * view * model };
    const cube_push push = { proj * view };

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

    VkRenderingAttachmentInfo color_attachment = {};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = target.image_view;
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

    VkRenderingInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset = { 0, 0 };
    rendering_info.renderArea.extent = extent;
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;

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

    vk::commands::push_constants(cmd, m_cube_pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(push), &push);

    disp.cmdDraw(cmd, CUBE_VERTEX_COUNT, 1, 0, 0);

    disp.cmdEndRendering(cmd);
}

}

void init()
{
    create_cube_resources();
    create_pipelines();
}

void cleanup()
{
    vk::shaders::destroy_pipeline(m_cube_pipeline);
    // vk::shaders::destroy_pipeline(m_cube_colors_pipeline);

    vk::descriptors::destroy_set_layout(m_cube_set_layout);
    vk::descriptors::destroy_pool(m_descriptor_pool);

    vk::resources::destroy_buffer(m_cube_normal_buffer);
    vk::resources::destroy_buffer(m_cube_face_buffer);
    vk::resources::destroy_buffer(m_cube_pos_buffer);

    m_cube_pipeline = {};
    // m_cube_colors_pipeline = {};

    m_descriptor_pool = VK_NULL_HANDLE;
    m_cube_set_layout = VK_NULL_HANDLE;
    m_cube_set = VK_NULL_HANDLE;

    // m_cube_rot = 0.0f;
}

void draw_all(VkCommandBuffer cmd, const swapchain_image_data& target, VkExtent2D extent)
{
    // randomize_cube_colors(cmd);
    draw_cube(cmd, target, extent);
}

}
