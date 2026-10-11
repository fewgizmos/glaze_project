#include "glp/glaze/rendering/scene_render/objects_pipeline.h"

#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/materials.h"
#include "glp/glaze/rendering/scene_render/resources/meshes.h"
#include "glp/glaze/rendering/scene_render/resources/models.h"
#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

namespace glaze::rendering::scene_render::objects_pipeline
{

namespace
{

struct render_object
{
    const mesh* target_mesh = nullptr;
    const material* target_material = nullptr;

    glm::mat4 transform = glm::mat4(1.0f);
};

struct object_data
{
    glm::mat4 model;
    VkDeviceAddress vertices;
    uint32_t vertex_count;
    uint32_t material_index;
};

constexpr const char* FOX_MODEL_PATH = "source/glaze_project_resources/gltf/fox/Fox.gltf";

constexpr float FOX_SCALE = 0.02f;

const glm::vec3 FOX_POSITIONS[] = {
    glm::vec3(-1.0f, 0.0f, 0.0f),
    glm::vec3( 1.0f, 0.0f, 0.0f),
    glm::vec3( 6.0f, 0.0f, 6.0f)
};

constexpr uint32_t OBJECTS_START_CAPACITY   = 64;
constexpr uint32_t MATERIALS_START_CAPACITY = 64;

const shader* m_base_model_shader = nullptr;

const model* m_fox_model = nullptr;

std::vector<render_object> m_objects;

std::vector<object_data> m_objects_data;

std::vector<glm::mat4> m_node_matrices;

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
    base_model_create_info.set_layouts = { scene_render::it::get_scene_set_layout() };
    base_model_create_info.push_constant_size = sizeof(VkDeviceAddress);
    base_model_create_info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    m_base_model_shader = res::shaders::build(
        "base_model",
        "base_model.vert",
        "base_model.frag",
        "",
        base_model_create_info,
        {}
    );
}

void add_model(const model* target, const glm::mat4& transform)
{
    m_node_matrices.resize(target->nodes.size());

    for (size_t i = 0; i < target->nodes.size(); i++) {
        const model_node& node = target->nodes[i];

        m_node_matrices[i] = (node.parent < 0 ? transform : m_node_matrices[node.parent]) * node.local;

        for (const model_part& part : node.parts) {
            m_objects.push_back({ part.target_mesh, part.target_material, m_node_matrices[i] });
        }
    }
}

void handle_objects()
{
    m_objects.clear();

    for (const glm::vec3& position : FOX_POSITIONS) {
        const glm::mat4 placed = glm::translate(glm::mat4(1.0f), position);

        add_model(m_fox_model, glm::scale(placed, glm::vec3(FOX_SCALE)));
    }

    std::sort(m_objects.begin(), m_objects.end(), [](const render_object& left, const render_object& right) {
        const shader* left_shader  = left.target_material->target_shader;
        const shader* right_shader = right.target_material->target_shader;

        if (left_shader != right_shader) {
            return std::less<const shader*>()(left_shader, right_shader);
        }

        return std::less<const mesh*>()(left.target_mesh, right.target_mesh);
    });
}

void handle_buffers()
{
    const uint32_t frame_index = vk::frame::get_current_frame_index();

    m_objects_data.clear();

    for (const render_object& target : m_objects) {
        m_objects_data.push_back({
            target.transform,
            target.target_mesh->vertex_buffer.address,
            target.target_mesh->vertex_count,
            target.target_material->index
        });
    }

    const std::vector<material_data>& materials_data = res::materials::get_data();

    upload_grown(m_object_buffers[frame_index], m_objects_data.data(), sizeof(object_data) * m_objects_data.size());

    upload_grown(
        m_material_buffers[frame_index],
        materials_data.data(),
        sizeof(material_data) * materials_data.size()
    );
}

void bind_shader(const shader* target)
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const VkDescriptorSet scene_set = scene_render::it::get_scene_set();

    const VkDeviceAddress push = scene_render::it::get_frame_data_address();

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

    m_fox_model = res::models::make(FOX_MODEL_PATH);
}

void clean_all()
{
    res::models::set_unused(m_fox_model);

    m_fox_model = nullptr;

    destroy_buffers();

    m_objects.clear();
    m_objects_data.clear();

    m_base_model_shader = nullptr;
}

void handle_all()
{
    handle_objects();

    handle_buffers();
}

void draw_all()
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const uint32_t objects_count = static_cast<uint32_t>(m_objects.size());

    const shader* bound_shader = nullptr;

    uint32_t first = 0;

    while (first < objects_count) {
        const render_object& target = m_objects[first];

        const shader* target_shader = target.target_material->target_shader;

        uint32_t count = 1;

        while (first + count < objects_count) {
            const render_object& next = m_objects[first + count];

            if (next.target_mesh != target.target_mesh || next.target_material->target_shader != target_shader) {
                break;
            }

            count++;
        }

        if (target_shader != bound_shader) {
            bound_shader = target_shader;

            bind_shader(bound_shader);
        }

        disp.cmdBindIndexBuffer(cmd, target.target_mesh->index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);

        disp.cmdDrawIndexed(cmd, target.target_mesh->index_count, count, 0, 0, first);

        first += count;
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
