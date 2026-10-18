#include "glp/glaze/rendering/scene_render/frame_pipeline.h"

#include "glp/glaze/rendering/scene_render/environment.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/materials.h"
#include "glp/glaze/rendering/scene_render/resources/meshes.h"
#include "glp/glaze/rendering/scene_render/resources/shaders.h"
#include "glp/glaze/rendering/scene_render/resources/shaders_stdlib.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"

#include "glp/glaze/rendering/defines.h"

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <array>
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
    glm::mat4 previous_model;
    VkDeviceAddress vertices;
    uint32_t vertex_count;
    uint32_t material_index;
    VkDeviceAddress indices;
    uint32_t index_count;
    uint32_t padding0;
};

constexpr uint32_t OBJECTS_START_CAPACITY   = 64;
constexpr uint32_t MATERIALS_START_CAPACITY = 64;
constexpr uint32_t LIGHTS_START_CAPACITY    = 16;

constexpr uint32_t LIGHT_TYPE_DIRECTIONAL = 0;

const glm::vec3 LUMINANCE_WEIGHTS = glm::vec3(0.2126f, 0.7152f, 0.0722f);

std::vector<gpu_model_part> m_objects;

std::vector<object_data> m_objects_data;

std::vector<gpu_light> m_lights;

int32_t m_shadow_light = -1;

struct draw_item
{
    uint32_t first_object;
    uint32_t instance_count;
    uint32_t first_index;
    uint32_t index_count;
};

enum class visibility
{
    outside,
    partial,
    inside
};

constexpr uint32_t FRUSTUM_PLANES = 6;

constexpr float MIN_PLANE_LENGTH = 0.000001f;

constexpr uint32_t MAX_RANGE_GAP_INDICES = 2 * 128 * 3;
constexpr size_t   MAX_OBJECT_RANGES     = 8;
constexpr size_t   CLUSTERS_PER_GROUP    = 16;

std::array<glm::vec4, FRUSTUM_PLANES> m_frustum = {};
std::array<glm::vec4, FRUSTUM_PLANES> m_local_frustum = {};

std::vector<draw_item> m_draw_items;

gpu_camera m_camera;
bool m_has_camera = false;

gpu_post m_post;

gpu_environment m_environment;
bool m_has_environment = false;

gpu_atmosphere m_atmosphere;
bool m_has_atmosphere = false;

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

void extract_frustum()
{
    const glm::mat4& m = m_camera.view_proj;

    const glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
    const glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);

    m_frustum = { row3 + row0, row3 - row0, row3 + row1, row3 - row1, row2, row3 - row2 };

    for (glm::vec4& plane : m_frustum) {
        const float length = glm::length(glm::vec3(plane));

        plane = length > MIN_PLANE_LENGTH ? plane / length : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }
}

visibility classify(const std::array<glm::vec4, FRUSTUM_PLANES>& planes, const glm::vec3& bounds_min, const glm::vec3& bounds_max)
{
    visibility result = visibility::inside;

    for (const glm::vec4& plane : planes) {
        const float positive_x = plane.x >= 0.0f ? bounds_max.x : bounds_min.x;
        const float positive_y = plane.y >= 0.0f ? bounds_max.y : bounds_min.y;
        const float positive_z = plane.z >= 0.0f ? bounds_max.z : bounds_min.z;

        if (plane.x * positive_x + plane.y * positive_y + plane.z * positive_z + plane.w < 0.0f) {
            return visibility::outside;
        }

        const float negative_x = plane.x >= 0.0f ? bounds_min.x : bounds_max.x;
        const float negative_y = plane.y >= 0.0f ? bounds_min.y : bounds_max.y;
        const float negative_z = plane.z >= 0.0f ? bounds_min.z : bounds_max.z;

        if (plane.x * negative_x + plane.y * negative_y + plane.z * negative_z + plane.w < 0.0f) {
            result = visibility::partial;
        }
    }

    return result;
}

void append_range(uint32_t object_index, uint32_t first_index, uint32_t index_count, uint32_t& range_first, uint32_t& range_count)
{
    if (range_count > 0 && first_index - (range_first + range_count) <= MAX_RANGE_GAP_INDICES) {
        range_count = first_index + index_count - range_first;
        return;
    }

    if (range_count > 0) {
        m_draw_items.push_back({ object_index, 1, range_first, range_count });
    }

    range_first = first_index;
    range_count = index_count;
}

void emit_clusters(uint32_t object_index)
{
    const gpu_model_part& part = m_objects[object_index];

    const glm::mat4 transposed = glm::transpose(part.transform);

    for (uint32_t i = 0; i < FRUSTUM_PLANES; i++) {
        m_local_frustum[i] = transposed * m_frustum[i];
    }

    const size_t items_start = m_draw_items.size();

    uint32_t range_first = 0;
    uint32_t range_count = 0;

    const std::vector<mesh_cluster>& clusters = part.target_mesh->clusters;
    const std::vector<mesh_cluster>& groups   = part.target_mesh->cluster_groups;

    for (size_t group_index = 0; group_index < groups.size(); group_index++) {
        const mesh_cluster& group = groups[group_index];

        const visibility group_state = classify(m_local_frustum, group.bounds_min, group.bounds_max);

        if (group_state == visibility::outside) {
            continue;
        }

        if (group_state == visibility::inside) {
            append_range(object_index, group.first_index, group.index_count, range_first, range_count);
            continue;
        }

        const size_t first_cluster = group_index * CLUSTERS_PER_GROUP;
        const size_t last_cluster  = std::min(first_cluster + CLUSTERS_PER_GROUP, clusters.size());

        for (size_t i = first_cluster; i < last_cluster; i++) {
            const mesh_cluster& cluster = clusters[i];

            if (classify(m_local_frustum, cluster.bounds_min, cluster.bounds_max) != visibility::outside) {
                append_range(object_index, cluster.first_index, cluster.index_count, range_first, range_count);
            }
        }
    }

    if (range_count > 0) {
        m_draw_items.push_back({ object_index, 1, range_first, range_count });
    }

    if (m_draw_items.size() - items_start > MAX_OBJECT_RANGES) {
        m_draw_items.resize(items_start);
        m_draw_items.push_back({ object_index, 1, 0, part.target_mesh->index_count });
    }
}

void build_draw_items()
{
    m_draw_items.clear();

    extract_frustum();

    const uint32_t objects_count = static_cast<uint32_t>(m_objects.size());

    uint32_t first = 0;

    while (first < objects_count) {
        const gpu_model_part& target = m_objects[first];

        uint32_t count = 1;

        while (first + count < objects_count) {
            const gpu_model_part& next = m_objects[first + count];

            if (
                next.target_mesh != target.target_mesh
                || next.target_material->target_shader != target.target_material->target_shader
                || next.target_material->extra_set != target.target_material->extra_set
            ) {
                break;
            }

            count++;
        }

        if (target.target_material->target_shader->queue != render_queue::opaque) {
            m_draw_items.push_back({ first, count, 0, target.target_mesh->index_count });

            first += count;
            continue;
        }

        uint32_t run_first = first;
        uint32_t run_count = 0;

        for (uint32_t object_index = first; object_index < first + count; object_index++) {
            const gpu_model_part& part = m_objects[object_index];

            glm::vec3 bounds_min;
            glm::vec3 bounds_max;

            res::meshes::world_bounds(part.target_mesh->bounds_min, part.target_mesh->bounds_max, part.transform, bounds_min, bounds_max);

            const visibility state = classify(m_frustum, bounds_min, bounds_max);

            if (state == visibility::inside) {
                if (run_count == 0) {
                    run_first = object_index;
                }

                run_count++;
                continue;
            }

            if (run_count > 0) {
                m_draw_items.push_back({ run_first, run_count, 0, part.target_mesh->index_count });
                run_count = 0;
            }

            if (state == visibility::partial) {
                emit_clusters(object_index);
            }
        }

        if (run_count > 0) {
            m_draw_items.push_back({ run_first, run_count, 0, target.target_mesh->index_count });
        }

        first += count;
    }
}

void handle_shadow_light()
{
    m_shadow_light = -1;

    if (!m_post.shadows_enabled) {
        return;
    }

    float brightest = 0.0f;

    for (size_t i = 0; i < m_lights.size(); i++) {
        if (m_lights[i].type != static_cast<uint32_t>(LIGHT_TYPE_DIRECTIONAL)) {
            continue;
        }

        const float luminance = glm::dot(m_lights[i].color, LUMINANCE_WEIGHTS);

        if (luminance > brightest) {
            brightest      = luminance;
            m_shadow_light = static_cast<int32_t>(i);
        }
    }

    if (m_shadow_light >= 0) {
        m_lights[m_shadow_light].shadowed = 1;
    }
}

void handle_buffers()
{
    const uint32_t frame_index = vk::frame::get_current_frame_index();

    m_objects_data.clear();

    for (const gpu_model_part& target : m_objects) {
        m_objects_data.push_back({
            target.transform,
            target.previous_transform,
            target.target_mesh->vertex_buffer.address,
            target.target_mesh->vertex_count,
            target.target_material->index,
            target.target_mesh->index_buffer.address,
            target.target_mesh->index_count,
            0
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

    m_shadow_light = -1;

    m_has_camera = false;

    m_post = {};

    m_environment     = {};
    m_has_environment = false;

    m_atmosphere     = {};
    m_has_atmosphere = false;
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

void set_post(const gpu_post& post)
{
    m_post = post;
}

void set_environment(const gpu_environment& environment)
{
    m_environment     = environment;
    m_has_environment = true;
}

void set_atmosphere(const gpu_atmosphere& atmosphere)
{
    m_atmosphere     = atmosphere;
    m_has_atmosphere = true;
}

void submit_light(const gpu_light& light)
{
    m_lights.push_back(light);
}

bool has_camera() { return m_has_camera; }

const gpu_camera& get_camera() { return m_camera; }

const gpu_post& get_post() { return m_post; }

bool has_environment() { return m_has_environment; }

const gpu_environment& get_environment() { return m_environment; }

bool has_atmosphere() { return m_has_atmosphere; }

const gpu_atmosphere& get_atmosphere() { return m_atmosphere; }

void handle_all()
{
    if (!m_has_camera) {
        return;
    }

    handle_objects();

    build_draw_items();

    handle_shadow_light();

    handle_buffers();
}

void draw_all()
{
    if (!m_has_camera) {
        return;
    }

    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const shader* bound_shader = nullptr;

    VkDescriptorSet bound_extra_set = VK_NULL_HANDLE;

    const mesh* bound_mesh = nullptr;

    for (const draw_item& item : m_draw_items) {
        const gpu_model_part& target = m_objects[item.first_object];

        const shader* target_shader = target.target_material->target_shader;

        const VkDescriptorSet target_extra_set = target.target_material->extra_set;

        if (target_shader != bound_shader) {
            bound_shader = target_shader;

            bound_extra_set = VK_NULL_HANDLE;

            bind_shader(bound_shader);

            if (bound_shader->uses_environment) {
                bound_extra_set = m_has_environment ? m_environment.set : environment::get_default_set();

                bind_extra_set(bound_shader, bound_extra_set);
            }
        }

        if (target_extra_set != VK_NULL_HANDLE && target_extra_set != bound_extra_set) {
            bound_extra_set = target_extra_set;

            bind_extra_set(bound_shader, bound_extra_set);
        }

        if (target.target_mesh != bound_mesh) {
            bound_mesh = target.target_mesh;

            disp.cmdBindIndexBuffer(cmd, bound_mesh->index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
        }

        disp.cmdDrawIndexed(cmd, item.index_count, item.instance_count, item.first_index, 0, item.first_object);
    }
}

void draw_prepass()
{
    if (!m_has_camera) {
        return;
    }

    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const shader* bound_shader = nullptr;

    const mesh* bound_mesh = nullptr;

    for (const draw_item& item : m_draw_items) {
        const gpu_model_part& target = m_objects[item.first_object];

        if (target.target_material->target_shader->queue != render_queue::opaque) {
            continue;
        }

        const shader* target_shader = target.target_material->double_sided ? std_prepass_double_sided_shader() : std_prepass_shader();

        if (target_shader != bound_shader) {
            bound_shader = target_shader;

            bind_shader(bound_shader);
        }

        if (target.target_mesh != bound_mesh) {
            bound_mesh = target.target_mesh;

            disp.cmdBindIndexBuffer(cmd, bound_mesh->index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
        }

        disp.cmdDrawIndexed(cmd, item.index_count, item.instance_count, item.first_index, 0, item.first_object);
    }
}

const std::vector<gpu_model_part>& get_objects() { return m_objects; }

bool has_shadow_light() { return m_shadow_light >= 0; }

const gpu_light& get_shadow_light() { return m_lights[m_shadow_light]; }

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
