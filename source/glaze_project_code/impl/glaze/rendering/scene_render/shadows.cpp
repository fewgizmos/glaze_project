#include "glp/glaze/rendering/scene_render/shadows.h"

#include "glp/glaze/rendering/scene_render/frame_pipeline.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/materials.h"
#include "glp/glaze/rendering/scene_render/resources/meshes.h"
#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

namespace glaze::rendering::scene_render::shadows
{

namespace
{

struct level_data
{
    glm::ivec2 origin;
    float page_world;
    float depth_scale;

    glm::vec2 origin_offset;
    glm::vec2 padding0;
};

constexpr uint32_t LEVELS       = 16;
constexpr uint32_t PAGE_SIZE    = 128;
constexpr uint32_t LEVEL_PAGES  = 128;
constexpr uint32_t LEVEL_SLOTS  = LEVEL_PAGES * LEVEL_PAGES;
constexpr uint32_t POOL_PAGES   = 64;
constexpr uint32_t POOL_SIZE    = POOL_PAGES * PAGE_SIZE;
constexpr uint32_t POOL_COUNT   = POOL_PAGES * POOL_PAGES;
constexpr uint32_t LEVEL_REQUESTS = 2048;
constexpr uint32_t MAX_DRAWS    = 262144;
constexpr uint32_t DRAW_LISTS   = 2;

constexpr uint32_t LIST_OPAQUE = 0;
constexpr uint32_t LIST_MASKED = 1;

constexpr double HALF_WINDOW_PAGES   = static_cast<double>(LEVEL_PAGES) * 0.5;
constexpr double LEVEL0_EXTENT       = 0.5;
constexpr double MIN_DEPTH_HALF      = 2000.0;
constexpr double DEPTH_EXTENT_FACTOR = 2.0;

constexpr uint32_t GROUP_SIZE       = 64;
constexpr uint32_t MARK_GROUP_SIZE  = 8;
constexpr uint32_t MARK_BLOCK_SIZE  = 2;
constexpr uint32_t MARK_BLOCK_PIXELS = MARK_GROUP_SIZE * MARK_BLOCK_SIZE;
constexpr uint32_t ROW_GROUPS       = LEVEL_PAGES / GROUP_SIZE;
constexpr uint32_t ENTRY_COUNT      = LEVELS * LEVEL_SLOTS;

constexpr float DEPTH_BIAS_SLOPE = 1.5f;
constexpr float FAR_DEPTH        = 1.0f;

constexpr uint32_t START_CAPACITY = 64;

constexpr uint32_t SETTLE_FRAMES = 3;

struct params_data
{
    glm::vec4 light_x;
    glm::vec4 light_y;
    glm::vec4 light_z;

    level_data levels[LEVELS];

    glm::mat4 inv_view_proj;

    glm::vec3 camera_pos;
    float pixel_angle;

    float texel0;
    float lod_bias;
    float softness;
    float normal_bias;

    float tan_angular_radius;
    uint32_t debug_mode;
    uint32_t caster_count;
    uint32_t box_count;

    float contact_length;
    uint32_t padding0;
    uint32_t padding1;
    uint32_t padding2;

    VkDeviceAddress entries;
    VkDeviceAddress free_stack;
    VkDeviceAddress requests;
    VkDeviceAddress render_pages;
    VkDeviceAddress counters;
    VkDeviceAddress draws;
    VkDeviceAddress draw_info;
    VkDeviceAddress clear_args;
    VkDeviceAddress casters;
    VkDeviceAddress boxes;
    VkDeviceAddress level_requests;
};

struct counters_data
{
    uint32_t free_count;
    uint32_t request_count;
    uint32_t render_count;
    uint32_t failed_count;
    uint32_t draw_counts[DRAW_LISTS];
    uint32_t requested_count;
    uint32_t padding0;
};

struct draw_command
{
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t first_vertex;
    uint32_t first_instance;
};

struct caster_data
{
    glm::vec3 bounds_min;
    uint32_t object_index;
    glm::vec3 bounds_max;
    uint32_t list;
    uint32_t first_index;
    uint32_t index_count;
    uint32_t padding[2];
};

struct box_data
{
    glm::vec4 bounds_min;
    glm::vec4 bounds_max;
};

struct object_signature
{
    const mesh* target_mesh;
    const material* target_material;
    glm::mat4 transform;
};

struct compute_push
{
    VkDeviceAddress params;
};

image m_pool;

buffer m_entries;
buffer m_free_stack;
buffer m_requests;
buffer m_level_requests;
buffer m_render_pages;
buffer m_counters;
buffer m_draws;
buffer m_draw_info;
buffer m_clear_args;

buffer m_params_buffers[MAX_FRAMES_IN_FLIGHT];
buffer m_caster_buffers[MAX_FRAMES_IN_FLIGHT];
buffer m_box_buffers[MAX_FRAMES_IN_FLIGHT];
buffer m_stats_buffers[MAX_FRAMES_IN_FLIGHT];

bool m_stats_ready[MAX_FRAMES_IN_FLIGHT] = {};

VkDescriptorSetLayout m_mark_layout = VK_NULL_HANDLE;
VkDescriptorPool m_mark_pool = VK_NULL_HANDLE;
VkDescriptorSet m_mark_set = VK_NULL_HANDLE;

VkDescriptorSetLayout m_contact_layout = VK_NULL_HANDLE;
VkDescriptorPool m_contact_pool = VK_NULL_HANDLE;
VkDescriptorSet m_contact_set = VK_NULL_HANDLE;

VkSampler m_depth_sampler = VK_NULL_HANDLE;

VkImageView m_written_depth_view = VK_NULL_HANDLE;
VkImageView m_written_contact_depth_view = VK_NULL_HANDLE;

image m_contact;

const shader* m_reset_shader = nullptr;
const shader* m_invalidate_shader = nullptr;
const shader* m_mark_shader = nullptr;
const shader* m_allocate_shader = nullptr;
const shader* m_assign_shader = nullptr;
const shader* m_prioritize_shader = nullptr;
const shader* m_finalize_shader = nullptr;
const shader* m_cull_shader = nullptr;
const shader* m_clear_shader = nullptr;
const shader* m_depth_shader = nullptr;
const shader* m_masked_shader = nullptr;
const shader* m_contact_shader = nullptr;

std::vector<caster_data> m_casters;
std::vector<box_data> m_boxes;

std::vector<object_signature> m_signatures;
std::vector<object_signature> m_previous_signatures;

std::vector<object_signature> m_object_order;
std::vector<object_signature> m_previous_object_order;

std::vector<object_signature> m_changed;

struct view_key
{
    glm::mat4 view_proj;
    glm::vec3 camera;

    float lod_bias;
    float softness;
    float normal_bias;

    uint32_t width;
    uint32_t height;
};

view_key m_view_key = {};

bool m_casters_changed = true;

uint32_t m_static_frames = 0;

bool m_pages_static = false;

glm::vec3 m_light_direction = glm::vec3(0.0f);

bool m_active     = false;
bool m_was_active = false;
bool m_needs_reset = true;

debug_mode m_debug_mode = debug_mode::none;

shadow_stats m_stats;

buffer create_device_buffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
    return vk::resources::create_buffer(
        size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage
    );
}

buffer create_host_buffer(VkDeviceSize size)
{
    return vk::resources::create_buffer(
        size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
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

        target = create_host_buffer(grown);
    }

    vk::resources::upload_buffer(target, data, size);
}

const shader* build_compute(const std::string& name, const std::string& file, VkDescriptorSetLayout set_layout)
{
    compute_pipeline_create_info info;
    info.push_constant_size = sizeof(compute_push);

    if (set_layout != VK_NULL_HANDLE) {
        info.set_layouts = { set_layout };
    }

    return res::shaders::build(name, "", "", file, {}, info, render_queue::opaque);
}

const shader* build_graphics(const std::string& name, const std::string& vert_file, const std::string& frag_file, VkCompareOp compare, float slope_bias)
{
    graphics_pipeline_create_info info;
    info.depth_format         = VK_FORMAT_D32_SFLOAT;
    info.depth_compare        = compare;
    info.depth_clamp          = true;
    info.depth_bias_slope     = slope_bias;
    info.cull_mode            = VK_CULL_MODE_NONE;
    info.set_layouts          = { it::get_scene_set_layout() };
    info.push_constant_size   = sizeof(VkDeviceAddress);
    info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    return res::shaders::build(name, vert_file, frag_file, "", info, {}, render_queue::opaque);
}

void create_resources()
{
    m_pool = vk::resources::create_image(
        { POOL_SIZE, POOL_SIZE, 1 },
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    m_entries      = create_device_buffer(sizeof(uint32_t) * 4 * ENTRY_COUNT, 0);
    m_free_stack   = create_device_buffer(sizeof(uint32_t) * POOL_COUNT, 0);
    m_requests       = create_device_buffer(sizeof(uint32_t) * LEVEL_REQUESTS * LEVELS, 0);
    m_level_requests = create_device_buffer(sizeof(uint32_t) * LEVELS * 2, 0);
    m_render_pages = create_device_buffer(sizeof(uint32_t) * POOL_COUNT, 0);
    m_counters     = create_device_buffer(sizeof(counters_data), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    m_draws        = create_device_buffer(sizeof(draw_command) * MAX_DRAWS * DRAW_LISTS, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    m_draw_info    = create_device_buffer(sizeof(uint32_t) * 2 * MAX_DRAWS * DRAW_LISTS, 0);
    m_clear_args   = create_device_buffer(sizeof(draw_command), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_params_buffers[i] = create_host_buffer(sizeof(params_data));
        m_caster_buffers[i] = create_host_buffer(sizeof(caster_data) * START_CAPACITY);
        m_box_buffers[i]    = create_host_buffer(sizeof(box_data) * START_CAPACITY);

        m_stats_buffers[i] = vk::resources::create_buffer(
            sizeof(counters_data),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
        );

        m_stats_ready[i] = false;
    }

    vk::commands::immediate_submit([](VkCommandBuffer cmd) {
        vk::commands::transition_image(
            cmd,
            m_pool.handle,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE,
            0,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT
        );

        const VkClearDepthStencilValue clear_value = { FAR_DEPTH, 0 };

        VkImageSubresourceRange range = {};
        range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        range.levelCount = 1;
        range.layerCount = 1;

        vk::device::get_disp().cmdClearDepthStencilImage(cmd, m_pool.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_value, 1, &range);

        vk::commands::transition_image(
            cmd,
            m_pool.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT
        );
    });
}

void destroy_resources()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::resources::destroy_buffer(m_stats_buffers[i]);
        vk::resources::destroy_buffer(m_box_buffers[i]);
        vk::resources::destroy_buffer(m_caster_buffers[i]);
        vk::resources::destroy_buffer(m_params_buffers[i]);
    }

    vk::resources::destroy_buffer(m_clear_args);
    vk::resources::destroy_buffer(m_draw_info);
    vk::resources::destroy_buffer(m_draws);
    vk::resources::destroy_buffer(m_counters);
    vk::resources::destroy_buffer(m_render_pages);
    vk::resources::destroy_buffer(m_level_requests);
    vk::resources::destroy_buffer(m_requests);
    vk::resources::destroy_buffer(m_free_stack);
    vk::resources::destroy_buffer(m_entries);

    vk::resources::destroy_image(m_pool);
}

bool signature_less(const object_signature& left, const object_signature& right)
{
    if (left.target_mesh != right.target_mesh) {
        return std::less<const mesh*>()(left.target_mesh, right.target_mesh);
    }

    if (left.target_material != right.target_material) {
        return std::less<const material*>()(left.target_material, right.target_material);
    }

    return std::memcmp(&left.transform, &right.transform, sizeof(glm::mat4)) < 0;
}

bool casts_shadow(const gpu_model_part& part)
{
    return part.target_material->target_shader->queue == render_queue::opaque && part.target_material->cast_shadows;
}

bool same_signature(const object_signature& left, const object_signature& right)
{
    return left.target_mesh == right.target_mesh
        && left.target_material == right.target_material
        && std::memcmp(&left.transform, &right.transform, sizeof(glm::mat4)) == 0;
}

void build_casters(const std::vector<gpu_model_part>& objects)
{
    m_casters.clear();

    for (size_t i = 0; i < objects.size(); i++) {
        const gpu_model_part& part = objects[i];

        if (!casts_shadow(part)) {
            continue;
        }

        const uint32_t list = part.target_material->alpha_mode == material_alpha_mode::mask ? LIST_MASKED : LIST_OPAQUE;

        for (const mesh_cluster& cluster : part.target_mesh->clusters) {
            caster_data caster = {};

            res::meshes::world_bounds(cluster.bounds_min, cluster.bounds_max, part.transform, caster.bounds_min, caster.bounds_max);

            caster.object_index = static_cast<uint32_t>(i);
            caster.list         = list;
            caster.first_index  = cluster.first_index;
            caster.index_count  = cluster.index_count;

            m_casters.push_back(caster);
        }
    }
}

void collect_casters()
{
    m_signatures.clear();
    m_object_order.clear();

    const std::vector<gpu_model_part>& objects = frame_pipe::get_objects();

    for (size_t i = 0; i < objects.size(); i++) {
        const gpu_model_part& part = objects[i];

        m_object_order.push_back({ part.target_mesh, part.target_material, part.transform });

        if (casts_shadow(part)) {
            m_signatures.push_back(m_object_order.back());
        }
    }

    const bool unchanged = m_object_order.size() == m_previous_object_order.size()
        && std::equal(m_object_order.begin(), m_object_order.end(), m_previous_object_order.begin(), same_signature);

    m_casters_changed = !unchanged;

    if (!unchanged) {
        build_casters(objects);
    }

    m_previous_object_order.swap(m_object_order);

    std::sort(m_signatures.begin(), m_signatures.end(), signature_less);
}

void collect_invalidations()
{
    m_boxes.clear();
    m_changed.clear();

    std::set_symmetric_difference(
        m_signatures.begin(),
        m_signatures.end(),
        m_previous_signatures.begin(),
        m_previous_signatures.end(),
        std::back_inserter(m_changed),
        signature_less
    );

    for (const object_signature& target : m_changed) {
        glm::vec3 bounds_min;
        glm::vec3 bounds_max;

        res::meshes::world_bounds(target.target_mesh->bounds_min, target.target_mesh->bounds_max, target.transform, bounds_min, bounds_max);

        m_boxes.push_back({ glm::vec4(bounds_min, 0.0f), glm::vec4(bounds_max, 0.0f) });
    }

    m_previous_signatures.swap(m_signatures);
}

void update_static_state()
{
    const gpu_camera& camera = frame_pipe::get_camera();
    const gpu_post& post     = frame_pipe::get_post();
    const VkExtent3D extent  = it::get_depth_image().extent;

    view_key key = {};

    key.view_proj   = camera.view_proj;
    key.camera      = camera.position;
    key.lod_bias    = post.shadow_lod_bias;
    key.softness    = post.shadow_softness;
    key.normal_bias = post.shadow_normal_bias;
    key.width       = extent.width;
    key.height      = extent.height;

    const bool same_view = std::memcmp(&key.view_proj, &m_view_key.view_proj, sizeof(glm::mat4)) == 0
        && key.camera == m_view_key.camera
        && key.lod_bias == m_view_key.lod_bias
        && key.softness == m_view_key.softness
        && key.normal_bias == m_view_key.normal_bias
        && key.width == m_view_key.width
        && key.height == m_view_key.height;

    const bool settled = m_stats.rendered_pages == 0 && m_stats.failed_pages == 0;

    const bool same = same_view && settled && !m_casters_changed && m_boxes.empty() && !m_needs_reset;

    m_view_key      = key;
    m_static_frames = same ? std::min(m_static_frames + 1, SETTLE_FRAMES + 1) : 0;
    m_pages_static  = m_static_frames > SETTLE_FRAMES;
}

void read_stats()
{
    const uint32_t frame = vk::frame::get_current_frame_index();

    if (!m_stats_ready[frame]) {
        return;
    }

    counters_data counters;

    vk::resources::download_buffer(m_stats_buffers[frame], &counters, sizeof(counters));

    m_stats.pool_pages      = POOL_COUNT;
    m_stats.free_pages      = counters.free_count;
    m_stats.requested_pages = counters.requested_count;
    m_stats.rendered_pages  = counters.render_count;
    m_stats.failed_pages    = counters.failed_count;
    m_stats.opaque_draws    = counters.draw_counts[LIST_OPAQUE];
    m_stats.masked_draws    = counters.draw_counts[LIST_MASKED];

    m_stats_ready[frame] = false;
}

void write_params()
{
    const uint32_t frame = vk::frame::get_current_frame_index();

    const gpu_camera& camera = frame_pipe::get_camera();
    const gpu_post& post     = frame_pipe::get_post();
    const gpu_light& light   = frame_pipe::get_shadow_light();

    const glm::vec3 axis_z = m_light_direction;
    const glm::vec3 up     = std::abs(axis_z.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 axis_x = glm::normalize(glm::cross(up, axis_z));
    const glm::vec3 axis_y = glm::cross(axis_z, axis_x);

    params_data data = {};

    data.light_x = glm::vec4(axis_x, 0.0f);
    data.light_y = glm::vec4(axis_y, 0.0f);
    data.light_z = glm::vec4(axis_z, 0.0f);

    const glm::dvec3 camera_pos = glm::dvec3(camera.position);

    const double camera_x = glm::dot(camera_pos, glm::dvec3(axis_x));
    const double camera_y = glm::dot(camera_pos, glm::dvec3(axis_y));

    for (uint32_t level = 0; level < LEVELS; level++) {
        const double extent     = LEVEL0_EXTENT * std::exp2(static_cast<double>(level));
        const double page_world = extent / static_cast<double>(LEVEL_PAGES);
        const double depth_half = std::max(MIN_DEPTH_HALF, extent * DEPTH_EXTENT_FACTOR);

        const double origin_x = std::floor(camera_x / page_world) - HALF_WINDOW_PAGES;
        const double origin_y = std::floor(camera_y / page_world) - HALF_WINDOW_PAGES;

        level_data& target = data.levels[level];

        target.origin        = glm::ivec2(static_cast<int32_t>(origin_x), static_cast<int32_t>(origin_y));
        target.page_world    = static_cast<float>(page_world);
        target.depth_scale   = static_cast<float>(1.0 / (2.0 * depth_half));
        target.origin_offset = glm::vec2(static_cast<float>(origin_x * page_world - camera_x), static_cast<float>(origin_y * page_world - camera_y));
        target.padding0      = glm::vec2(0.0f);
    }

    const VkExtent3D extent = it::get_depth_image().extent;

    data.inv_view_proj = camera.inv_view_proj;
    data.camera_pos    = camera.position;
    data.pixel_angle   = 2.0f / (std::abs(camera.proj[1][1]) * static_cast<float>(extent.height));

    data.texel0      = static_cast<float>(LEVEL0_EXTENT / static_cast<double>(LEVEL_PAGES * PAGE_SIZE));
    data.lod_bias    = post.shadow_lod_bias;
    data.softness    = post.shadow_softness;
    data.normal_bias = post.shadow_normal_bias;

    data.tan_angular_radius = light.sin_angular_radius / std::max(light.cos_angular_radius, 0.0001f);
    data.debug_mode         = static_cast<uint32_t>(m_debug_mode);
    data.caster_count       = static_cast<uint32_t>(m_casters.size());
    data.box_count          = static_cast<uint32_t>(m_boxes.size());
    data.contact_length     = post.contact_shadow_length;

    upload_grown(m_caster_buffers[frame], m_casters.data(), sizeof(caster_data) * m_casters.size());
    upload_grown(m_box_buffers[frame], m_boxes.data(), sizeof(box_data) * m_boxes.size());

    data.entries      = m_entries.address;
    data.free_stack   = m_free_stack.address;
    data.requests     = m_requests.address;
    data.render_pages = m_render_pages.address;
    data.counters     = m_counters.address;
    data.draws        = m_draws.address;
    data.draw_info    = m_draw_info.address;
    data.clear_args   = m_clear_args.address;
    data.casters      = m_caster_buffers[frame].address;
    data.boxes        = m_box_buffers[frame].address;

    data.level_requests = m_level_requests.address;

    vk::resources::upload_buffer(m_params_buffers[frame], &data, sizeof(data));
}

void compute_barrier(VkCommandBuffer cmd)
{
    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );
}

void run_compute(VkCommandBuffer cmd, const shader* target, uint32_t groups_x, uint32_t groups_y, uint32_t groups_z)
{
    const compute_push push = { get_params_address() };

    vk::commands::dispatch(cmd, target->compute, &push, sizeof(push), groups_x, groups_y, groups_z);

    compute_barrier(cmd);
}

void bind_graphics(VkCommandBuffer cmd, const shader* target)
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    const VkDeviceAddress frame = it::get_frame_data_address();

    const VkDescriptorSet scene_set = it::get_scene_set();

    disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, target->graphics.handle);

    vk::commands::push_constants(cmd, target->graphics.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(frame), &frame);

    disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, target->graphics.layout, 0, 1, &scene_set, 0, nullptr);
}

void render_pages(VkCommandBuffer cmd)
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    vk::commands::transition_image(
        cmd,
        m_pool.handle,
        VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        0,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    VkRenderingAttachmentInfo depth_attachment = {};
    depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_attachment.imageView = m_pool.view;
    depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset = { 0, 0 };
    rendering_info.renderArea.extent = { POOL_SIZE, POOL_SIZE };
    rendering_info.layerCount = 1;
    rendering_info.pDepthAttachment = &depth_attachment;

    disp.cmdBeginRendering(cmd, &rendering_info);

    VkViewport viewport = {};
    viewport.width    = static_cast<float>(POOL_SIZE);
    viewport.height   = static_cast<float>(POOL_SIZE);
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.extent = { POOL_SIZE, POOL_SIZE };

    disp.cmdSetViewport(cmd, 0, 1, &viewport);
    disp.cmdSetScissor(cmd, 0, 1, &scissor);

    bind_graphics(cmd, m_clear_shader);

    disp.cmdDrawIndirect(cmd, m_clear_args.handle, 0, 1, sizeof(draw_command));

    bind_graphics(cmd, m_depth_shader);

    disp.cmdDrawIndirectCount(
        cmd,
        m_draws.handle,
        0,
        m_counters.handle,
        offsetof(counters_data, draw_counts) + sizeof(uint32_t) * LIST_OPAQUE,
        MAX_DRAWS,
        sizeof(draw_command)
    );

    bind_graphics(cmd, m_masked_shader);

    disp.cmdDrawIndirectCount(
        cmd,
        m_draws.handle,
        sizeof(draw_command) * MAX_DRAWS * LIST_MASKED,
        m_counters.handle,
        offsetof(counters_data, draw_counts) + sizeof(uint32_t) * LIST_MASKED,
        MAX_DRAWS,
        sizeof(draw_command)
    );

    disp.cmdEndRendering(cmd);

    vk::commands::transition_image(
        cmd,
        m_pool.handle,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );
}

void create_contact_image(VkExtent3D extent)
{
    m_contact = vk::resources::create_image(
        { extent.width, extent.height, 1 },
        VK_FORMAT_R8_UNORM,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    vk::descriptors::write_image(m_contact_set, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_contact.view, VK_IMAGE_LAYOUT_GENERAL);
}

void render_contact(VkCommandBuffer cmd)
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    vk::commands::transition_image(
        cmd,
        m_contact.handle,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        0,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    const VkDeviceAddress frame = it::get_frame_data_address();

    disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_contact_shader->compute.layout, 0, 1, &m_contact_set, 0, nullptr);

    vk::commands::dispatch(
        cmd,
        m_contact_shader->compute,
        &frame,
        sizeof(frame),
        (m_contact.extent.width + MARK_GROUP_SIZE - 1) / MARK_GROUP_SIZE,
        (m_contact.extent.height + MARK_GROUP_SIZE - 1) / MARK_GROUP_SIZE,
        1
    );

    vk::commands::transition_image(
        cmd,
        m_contact.handle,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );
}

void copy_stats(VkCommandBuffer cmd)
{
    const uint32_t frame = vk::frame::get_current_frame_index();

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT
    );

    VkBufferCopy region = {};
    region.size = sizeof(counters_data);

    vk::device::get_disp().cmdCopyBuffer(cmd, m_counters.handle, m_stats_buffers[frame].handle, 1, &region);

    m_stats_ready[frame] = true;
}

}

void init_all()
{
    create_resources();

    m_mark_layout = vk::descriptors::create_set_layout({
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    });

    m_mark_pool = vk::descriptors::create_pool(1, { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 } });
    m_mark_set  = vk::descriptors::allocate_set(m_mark_pool, m_mark_layout);

    sampler_create_info sampler_info;
    sampler_info.mag_filter = VK_FILTER_NEAREST;
    sampler_info.min_filter = VK_FILTER_NEAREST;
    sampler_info.mip_mode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.wrap_u     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_v     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_w     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropy = false;

    m_depth_sampler = vk::samplers::get(vk::samplers::make(sampler_info));

    m_contact_layout = vk::descriptors::create_set_layout({
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    });

    m_contact_pool = vk::descriptors::create_pool(
        1,
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1 }
        }
    );

    m_contact_set = vk::descriptors::allocate_set(m_contact_pool, m_contact_layout);

    create_contact_image(it::get_depth_image().extent);

    m_reset_shader      = build_compute("vsm_reset", "sys/shadows/vsm_reset.comp", VK_NULL_HANDLE);
    m_invalidate_shader = build_compute("vsm_invalidate", "sys/shadows/vsm_invalidate.comp", VK_NULL_HANDLE);
    m_mark_shader       = build_compute("vsm_mark", "sys/shadows/vsm_mark.comp", m_mark_layout);
    m_allocate_shader   = build_compute("vsm_allocate", "sys/shadows/vsm_allocate.comp", VK_NULL_HANDLE);
    m_assign_shader     = build_compute("vsm_assign", "sys/shadows/vsm_assign.comp", VK_NULL_HANDLE);
    m_prioritize_shader = build_compute("vsm_prioritize", "sys/shadows/vsm_prioritize.comp", VK_NULL_HANDLE);
    m_finalize_shader   = build_compute("vsm_finalize", "sys/shadows/vsm_finalize.comp", VK_NULL_HANDLE);
    m_cull_shader       = build_compute("vsm_cull", "sys/shadows/vsm_cull.comp", VK_NULL_HANDLE);

    m_clear_shader  = build_graphics("vsm_clear", "sys/shadows/vsm_clear.vert", "", VK_COMPARE_OP_ALWAYS, 0.0f);
    m_depth_shader  = build_graphics("vsm_depth", "sys/shadows/vsm_depth.vert", "", VK_COMPARE_OP_LESS_OR_EQUAL, DEPTH_BIAS_SLOPE);
    m_masked_shader = build_graphics("vsm_masked", "sys/shadows/vsm_depth.vert", "sys/shadows/vsm_masked.frag", VK_COMPARE_OP_LESS_OR_EQUAL, DEPTH_BIAS_SLOPE);

    compute_pipeline_create_info contact_info;
    contact_info.set_layouts        = { m_contact_layout };
    contact_info.push_constant_size = sizeof(VkDeviceAddress);

    m_contact_shader = res::shaders::build("contact_shadows", "", "", "sys/shadows/contact_shadows.comp", {}, contact_info, render_queue::opaque);

    m_needs_reset = true;
}

void clean_all()
{
    vk::descriptors::destroy_pool(m_mark_pool);
    vk::descriptors::destroy_set_layout(m_mark_layout);

    m_mark_layout = VK_NULL_HANDLE;
    m_mark_pool   = VK_NULL_HANDLE;
    m_mark_set    = VK_NULL_HANDLE;

    vk::descriptors::destroy_pool(m_contact_pool);
    vk::descriptors::destroy_set_layout(m_contact_layout);

    m_contact_layout = VK_NULL_HANDLE;
    m_contact_pool   = VK_NULL_HANDLE;
    m_contact_set    = VK_NULL_HANDLE;

    vk::resources::destroy_image(m_contact);

    m_depth_sampler              = VK_NULL_HANDLE;
    m_written_depth_view         = VK_NULL_HANDLE;
    m_written_contact_depth_view = VK_NULL_HANDLE;

    destroy_resources();

    m_casters.clear();
    m_boxes.clear();
    m_signatures.clear();
    m_previous_signatures.clear();
    m_object_order.clear();
    m_previous_object_order.clear();

    m_active      = false;
    m_was_active  = false;
    m_needs_reset = true;
}

void handle_targets()
{
    const image& depth = it::get_depth_image();

    if (depth.extent.width != m_contact.extent.width || depth.extent.height != m_contact.extent.height) {
        vk::device::get_disp().deviceWaitIdle();

        vk::resources::destroy_image(m_contact);

        create_contact_image(depth.extent);
    }

    if (depth.view != m_written_contact_depth_view) {
        vk::descriptors::write_image(m_contact_set, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, depth.view, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, m_depth_sampler);

        m_written_contact_depth_view = depth.view;
    }
}

void handle_all()
{
    read_stats();

    m_active = frame_pipe::has_camera() && frame_pipe::has_shadow_light();

    if (!m_active) {
        m_was_active = false;
        return;
    }

    const glm::vec3 direction = glm::normalize(frame_pipe::get_shadow_light().direction);

    if (!m_was_active || direction != m_light_direction) {
        m_needs_reset = true;
    }

    m_was_active      = true;
    m_light_direction = direction;

    collect_casters();
    collect_invalidations();

    m_stats.casters       = static_cast<uint32_t>(m_casters.size());
    m_stats.invalidations = static_cast<uint32_t>(m_boxes.size());

    if (m_needs_reset) {
        m_boxes.clear();
    }

    update_static_state();

    const image& depth = it::get_depth_image();

    if (depth.view != m_written_depth_view) {
        vk::descriptors::write_image(m_mark_set, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, depth.view, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, m_depth_sampler);

        m_written_depth_view = depth.view;
    }

    write_params();
}

void dispatch()
{
    if (!m_active) {
        return;
    }

    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    if (m_pages_static) {
        render_contact(cmd);
        return;
    }

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT
    );

    if (m_needs_reset) {
        run_compute(cmd, m_reset_shader, (ENTRY_COUNT + GROUP_SIZE - 1) / GROUP_SIZE, 1, 1);

        m_needs_reset = false;
    }

    disp.cmdFillBuffer(cmd, m_counters.handle, sizeof(uint32_t), sizeof(counters_data) - sizeof(uint32_t), 0);
    disp.cmdFillBuffer(cmd, m_level_requests.handle, 0, m_level_requests.size, 0);

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    if (!m_boxes.empty()) {
        run_compute(cmd, m_invalidate_shader, static_cast<uint32_t>(m_boxes.size()), LEVELS, ROW_GROUPS);
    }

    const image& depth = it::get_depth_image();

    disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_mark_shader->compute.layout, 0, 1, &m_mark_set, 0, nullptr);

    run_compute(
        cmd,
        m_mark_shader,
        (depth.extent.width + MARK_BLOCK_PIXELS - 1) / MARK_BLOCK_PIXELS,
        (depth.extent.height + MARK_BLOCK_PIXELS - 1) / MARK_BLOCK_PIXELS,
        1
    );

    run_compute(cmd, m_allocate_shader, (ENTRY_COUNT + GROUP_SIZE - 1) / GROUP_SIZE, 1, 1);
    run_compute(cmd, m_prioritize_shader, 1, 1, 1);
    run_compute(cmd, m_assign_shader, (LEVEL_REQUESTS + GROUP_SIZE - 1) / GROUP_SIZE, LEVELS, 1);
    run_compute(cmd, m_finalize_shader, 1, 1, 1);

    if (!m_casters.empty()) {
        run_compute(cmd, m_cull_shader, static_cast<uint32_t>(m_casters.size()), LEVELS, ROW_GROUPS);
    }

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT
    );

    render_pages(cmd);

    render_contact(cmd);

    copy_stats(cmd);
}

bool is_active() { return m_active; }

VkDeviceAddress get_params_address()
{
    return m_params_buffers[vk::frame::get_current_frame_index()].address;
}

const image& get_pool_image() { return m_pool; }

const image& get_contact_image() { return m_contact; }

const shadow_stats& get_stats() { return m_stats; }

void set_debug_mode(debug_mode mode) { m_debug_mode = mode; }

debug_mode get_debug_mode() { return m_debug_mode; }

void request_reset() { m_needs_reset = true; }

}
