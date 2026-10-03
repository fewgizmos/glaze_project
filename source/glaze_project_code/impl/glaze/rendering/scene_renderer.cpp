#include "glp/glaze/rendering/scene_renderer.h"

#include "glp/glaze/rendering/resources/material.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/resources.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <vector>

namespace glaze::rendering::scene_renderer
{

namespace
{

struct helix_point
{
    glm::vec4 pos;
    glm::vec4 color;
};

struct helix_push
{
    glm::mat4 view_proj;
    VkDeviceAddress points;
    float size;
    float aspect;
};

struct helix_compute_push
{
    VkDeviceAddress points;
};

constexpr uint32_t HELIX_PAIRS = 96;
constexpr uint32_t HELIX_POINTS_PER_PAIR = 3;
constexpr uint32_t HELIX_POINT_COUNT = HELIX_PAIRS * HELIX_POINTS_PER_PAIR;

constexpr uint32_t COMPUTE_GROUP_SIZE = 64;

constexpr float HELIX_POINT_SIZE = 0.01f;

const material* m_helix_material = nullptr;

buffer m_helix_buffer;

void create_helix_buffer()
{
    const std::vector<helix_point> zeroed(HELIX_POINT_COUNT);

    m_helix_buffer = vk::resources::create_buffer(
        sizeof(helix_point) * HELIX_POINT_COUNT,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    vk::resources::upload_buffer(m_helix_buffer, zeroed.data(), sizeof(helix_point) * HELIX_POINT_COUNT);
}

void create_pipelines()
{
    graphics_pipeline_create_info graphics_create_info;
    graphics_create_info.color_format = vk::frame::get_swapchain().image_format;
    graphics_create_info.cull_mode = VK_CULL_MODE_NONE;
    graphics_create_info.push_constant_size = sizeof(helix_push);
    graphics_create_info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT;

    compute_pipeline_create_info compute_create_info;
    compute_create_info.push_constant_size = sizeof(helix_compute_push);

    m_helix_material = resources::materials::build(
        "helix",
        "helix.vert",
        "helix.frag",
        "helix.comp",
        graphics_create_info,
        compute_create_info
    );
}

void update_helix(VkCommandBuffer cmd)
{
    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    const helix_compute_push push = { m_helix_buffer.address };

    vk::commands::dispatch(
        cmd,
        m_helix_material->compute,
        &push,
        sizeof(push),
        (HELIX_POINT_COUNT + COMPUTE_GROUP_SIZE - 1) / COMPUTE_GROUP_SIZE,
        1,
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

void draw_helix(VkCommandBuffer cmd, VkExtent2D extent)
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);

    const glm::mat4 view = glm::lookAt(glm::vec3(2.5f, 3.0f, 2.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 0.1f, 100.0f);

    proj[1][1] *= -1.0f;

    disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_helix_material->graphics.handle);

    const helix_push push = { proj * view, m_helix_buffer.address, HELIX_POINT_SIZE, aspect };

    vk::commands::push_constants(cmd, m_helix_material->graphics.layout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(push), &push);

    disp.cmdDraw(cmd, 3 * 16, HELIX_POINT_COUNT, 0, 0);
}

}

void init()
{
    create_helix_buffer();

    create_pipelines();
}

void cleanup()
{
    vk::resources::destroy_buffer(m_helix_buffer);

    m_helix_material = nullptr;
}

void update_all(VkCommandBuffer cmd)
{
    update_helix(cmd);
}

void draw_all(VkCommandBuffer cmd, VkExtent2D extent)
{
    draw_helix(cmd, extent);
}

}
