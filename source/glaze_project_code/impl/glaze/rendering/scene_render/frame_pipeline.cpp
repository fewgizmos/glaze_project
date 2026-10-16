#include "glp/glaze/rendering/scene_render/frame_pipeline.h"

#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/materials.h"
#include "glp/glaze/rendering/scene_render/resources/meshes.h"
#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"

#include "glp/glaze/rendering/defines.h"

#include <glm/mat4x4.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

namespace glaze::rendering::scene_render::frame_pipe
{

namespace
{

struct object_data
{
    glm::mat4 model;
    VkDeviceAddress vertices;
    uint32_t vertex_count;
    uint32_t material_index;
};

constexpr uint32_t OBJECTS_START_CAPACITY   = 64;
constexpr uint32_t MATERIALS_START_CAPACITY = 64;
constexpr uint32_t LIGHTS_START_CAPACITY    = 16;

std::vector<gpu_model_part> m_objects;

std::vector<object_data> m_objects_data;

std::vector<gpu_light> m_lights;

gpu_camera m_camera;
bool m_has_camera = false;

buffer m_object_buffers[MAX_FRAMES_IN_FLIGHT];
buffer m_material_buffers[MAX_FRAMES_IN_FLIGHT];
buffer m_light_buffers[MAX_FRAMES_IN_FLIGHT];

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
        m_light_buffers[i]    = create_shader_buffer(sizeof(gpu_light) * LIGHTS_START_CAPACITY);
    }
}

void destroy_buffers()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::resources::destroy_buffer(m_light_buffers[i]);
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

void handle_objects()
{
    std::sort(m_objects.begin(), m_objects.end(), [](const gpu_model_part& left, const gpu_model_part& right) {
        const shader* left_shader  = left.target_material->target_shader;
        const shader* right_shader = right.target_material->target_shader;

        if (left_shader->queue != right_shader->queue) {
            return left_shader->queue < right_shader->queue;
        }

        if (left_shader != right_shader) {
            return std::less<const shader*>()(left_shader, right_shader);
        }

        const VkDescriptorSet left_set  = left.target_material->extra_set;
        const VkDescriptorSet right_set = right.target_material->extra_set;

        if (left_set != right_set) {
            return std::less<VkDescriptorSet>()(left_set, right_set);
        }

        return std::less<const mesh*>()(left.target_mesh, right.target_mesh);
    });
}

void handle_buffers()
{
    const uint32_t frame_index = vk::frame::get_current_frame_index();

    m_objects_data.clear();

    for (const gpu_model_part& target : m_objects) {
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

    upload_grown(m_light_buffers[frame_index], m_lights.data(), sizeof(gpu_light) * m_lights.size());
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

void bind_extra_set(const shader* target, VkDescriptorSet extra_set)
{
    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    vk::device::get_disp().cmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        target->graphics.layout,
        1,
        1,
        &extra_set,
        0,
        nullptr
    );
}

}

void init_all()
{
    create_buffers();
}

void clean_all()
{
    destroy_buffers();

    clear();

    m_objects_data.clear();
}

void clear()
{
    m_objects.clear();
    m_lights.clear();

    m_has_camera = false;
}

void set_camera(const gpu_camera& camera)
{
    m_camera     = camera;
    m_has_camera = true;
}

void submit_model(const gpu_model& model)
{
    for (const gpu_model_part& part : model.parts) {
        if (part.target_mesh == nullptr || part.target_material == nullptr || part.target_material->target_shader == nullptr) {
            continue;
        }

        m_objects.push_back(part);
    }
}

void submit_light(const gpu_light& light)
{
    m_lights.push_back(light);
}

bool has_camera() { return m_has_camera; }

const gpu_camera& get_camera() { return m_camera; }

void handle_all()
{
    if (!m_has_camera) {
        return;
    }

    handle_objects();

    handle_buffers();
}

void draw_all()
{
    if (!m_has_camera) {
        return;
    }

    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const uint32_t objects_count = static_cast<uint32_t>(m_objects.size());

    const shader* bound_shader = nullptr;

    VkDescriptorSet bound_extra_set = VK_NULL_HANDLE;

    uint32_t first = 0;

    while (first < objects_count) {
        const gpu_model_part& target = m_objects[first];

        const shader* target_shader = target.target_material->target_shader;

        const VkDescriptorSet target_extra_set = target.target_material->extra_set;

        uint32_t count = 1;

        while (first + count < objects_count) {
            const gpu_model_part& next = m_objects[first + count];

            if (
                next.target_mesh != target.target_mesh
                || next.target_material->target_shader != target_shader
                || next.target_material->extra_set != target_extra_set
            ) {
                break;
            }

            count++;
        }

        if (target_shader != bound_shader) {
            bound_shader = target_shader;

            bound_extra_set = VK_NULL_HANDLE;

            bind_shader(bound_shader);
        }

        if (target_extra_set != VK_NULL_HANDLE && target_extra_set != bound_extra_set) {
            bound_extra_set = target_extra_set;

            bind_extra_set(bound_shader, bound_extra_set);
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

VkDeviceAddress get_lights_address()
{
    return m_light_buffers[vk::frame::get_current_frame_index()].address;
}

uint32_t get_lights_count() { return static_cast<uint32_t>(m_lights.size()); }

}
