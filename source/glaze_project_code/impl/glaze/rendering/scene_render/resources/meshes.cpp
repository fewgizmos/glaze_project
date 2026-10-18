#include "glp/glaze/rendering/scene_render/resources/meshes.h"

#include "glp/glaze/rendering/defines.h"

#include <glm/common.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>

namespace glaze::rendering::scene_render::res::meshes
{

namespace
{

struct erased_mesh
{
    mesh target;
    int frames_left = 0;
};

std::unordered_map<const mesh*, std::unique_ptr<mesh>> m_meshes;

std::vector<erased_mesh> m_erased;

constexpr uint32_t TRIANGLE_INDICES  = 3;
constexpr uint32_t CLUSTER_TRIANGLES = 128;
constexpr size_t   GROUP_CLUSTERS    = 16;

constexpr float MORTON_GRID = 1023.0f;

void destroy_buffers(mesh& target)
{
    vk::resources::destroy_buffer(target.index_buffer);
    vk::resources::destroy_buffer(target.vertex_buffer);
}

uint32_t spread_bits(uint32_t value)
{
    value = (value | (value << 16)) & 0x030000FF;
    value = (value | (value << 8))  & 0x0300F00F;
    value = (value | (value << 4))  & 0x030C30C3;
    value = (value | (value << 2))  & 0x09249249;

    return value;
}

uint32_t morton_code(const glm::vec3& point, const glm::vec3& bounds_min, const glm::vec3& bounds_size)
{
    const glm::vec3 cell = glm::clamp((point - bounds_min) / glm::max(bounds_size, glm::vec3(std::numeric_limits<float>::min())), 0.0f, 1.0f) * MORTON_GRID;

    return spread_bits(static_cast<uint32_t>(cell.x)) | (spread_bits(static_cast<uint32_t>(cell.y)) << 1) | (spread_bits(static_cast<uint32_t>(cell.z)) << 2);
}

std::vector<uint32_t> sort_triangles(const std::vector<vertex>& vertices, const std::vector<uint32_t>& indices, const glm::vec3& bounds_min, const glm::vec3& bounds_max)
{
    const size_t triangle_count = indices.size() / TRIANGLE_INDICES;

    std::vector<std::pair<uint32_t, uint32_t>> keys(triangle_count);

    for (size_t i = 0; i < triangle_count; i++) {
        const glm::vec3 centroid = (
            vertices[indices[i * TRIANGLE_INDICES]].pos +
            vertices[indices[i * TRIANGLE_INDICES + 1]].pos +
            vertices[indices[i * TRIANGLE_INDICES + 2]].pos
        ) / 3.0f;

        keys[i] = { morton_code(centroid, bounds_min, bounds_max - bounds_min), static_cast<uint32_t>(i) };
    }

    std::sort(keys.begin(), keys.end());

    std::vector<uint32_t> sorted;
    sorted.reserve(triangle_count * TRIANGLE_INDICES);

    for (const auto& key : keys) {
        for (uint32_t corner = 0; corner < TRIANGLE_INDICES; corner++) {
            sorted.push_back(indices[key.second * TRIANGLE_INDICES + corner]);
        }
    }

    return sorted;
}

std::vector<mesh_cluster> build_clusters(const std::vector<vertex>& vertices, const std::vector<uint32_t>& indices)
{
    std::vector<mesh_cluster> clusters;

    const uint32_t cluster_indices = CLUSTER_TRIANGLES * TRIANGLE_INDICES;

    for (uint32_t first = 0; first < indices.size(); first += cluster_indices) {
        mesh_cluster cluster;

        cluster.first_index = first;
        cluster.index_count = std::min(cluster_indices, static_cast<uint32_t>(indices.size()) - first);
        cluster.bounds_min  = glm::vec3(std::numeric_limits<float>::max());
        cluster.bounds_max  = glm::vec3(std::numeric_limits<float>::lowest());

        for (uint32_t i = first; i < first + cluster.index_count; i++) {
            cluster.bounds_min = glm::min(cluster.bounds_min, vertices[indices[i]].pos);
            cluster.bounds_max = glm::max(cluster.bounds_max, vertices[indices[i]].pos);
        }

        clusters.push_back(cluster);
    }

    return clusters;
}

std::vector<mesh_cluster> build_cluster_groups(const std::vector<mesh_cluster>& clusters)
{
    std::vector<mesh_cluster> groups;

    for (size_t first = 0; first < clusters.size(); first += GROUP_CLUSTERS) {
        const size_t last = std::min(first + GROUP_CLUSTERS, clusters.size());

        mesh_cluster group;

        group.first_index = clusters[first].first_index;
        group.index_count = clusters[last - 1].first_index + clusters[last - 1].index_count - group.first_index;
        group.bounds_min  = clusters[first].bounds_min;
        group.bounds_max  = clusters[first].bounds_max;

        for (size_t i = first + 1; i < last; i++) {
            group.bounds_min = glm::min(group.bounds_min, clusters[i].bounds_min);
            group.bounds_max = glm::max(group.bounds_max, clusters[i].bounds_max);
        }

        groups.push_back(group);
    }

    return groups;
}

}

const mesh* build(const std::vector<vertex>& vertices, const std::vector<uint32_t>& indices)
{
    const VkDeviceSize vertices_size = sizeof(vertex) * vertices.size();
    const VkDeviceSize indices_size  = sizeof(uint32_t) * indices.size();

    std::unique_ptr<mesh> created = std::make_unique<mesh>();

    created->vertex_count = static_cast<uint32_t>(vertices.size());
    created->index_count  = static_cast<uint32_t>(indices.size());

    created->bounds_min = glm::vec3(std::numeric_limits<float>::max());
    created->bounds_max = glm::vec3(std::numeric_limits<float>::lowest());

    for (const vertex& point : vertices) {
        created->bounds_min = glm::min(created->bounds_min, point.pos);
        created->bounds_max = glm::max(created->bounds_max, point.pos);
    }

    const std::vector<uint32_t> sorted = sort_triangles(vertices, indices, created->bounds_min, created->bounds_max);

    created->index_count = static_cast<uint32_t>(sorted.size());
    created->clusters    = build_clusters(vertices, sorted);

    created->cluster_groups = build_cluster_groups(created->clusters);

    created->vertex_buffer = vk::resources::create_buffer(
        vertices_size,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    );

    created->index_buffer = vk::resources::create_buffer(
        indices_size,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    );

    vk::resources::queue_upload(created->vertex_buffer, vertices.data(), vertices_size);
    vk::resources::queue_upload(created->index_buffer, sorted.data(), sizeof(uint32_t) * sorted.size());

    const mesh* result = created.get();

    m_meshes.emplace(result, std::move(created));

    return result;
}

void world_bounds(const glm::vec3& local_min, const glm::vec3& local_max, const glm::mat4& transform, glm::vec3& bounds_min, glm::vec3& bounds_max)
{
    const glm::vec3 center = glm::vec3(transform * glm::vec4((local_min + local_max) * 0.5f, 1.0f));
    const glm::vec3 extent = (local_max - local_min) * 0.5f;

    const glm::vec3 world_extent = glm::abs(glm::vec3(transform[0])) * extent.x
                                 + glm::abs(glm::vec3(transform[1])) * extent.y
                                 + glm::abs(glm::vec3(transform[2])) * extent.z;

    bounds_min = center - world_extent;
    bounds_max = center + world_extent;
}

void erase(const mesh* target)
{
    const auto found = m_meshes.find(target);

    if (found == m_meshes.end()) {
        return;
    }

    m_erased.push_back({ *found->second, MAX_FRAMES_IN_FLIGHT });

    m_meshes.erase(found);
}

void erase_all()
{
    for (auto& entry : m_meshes) {
        destroy_buffers(*entry.second);
    }

    for (erased_mesh& erased : m_erased) {
        destroy_buffers(erased.target);
    }

    m_meshes.clear();
    m_erased.clear();
}

void destroy_erased()
{
    for (auto it = m_erased.begin(); it != m_erased.end();) {
        if (it->frames_left > 0) {
            it->frames_left--;
            it++;

            continue;
        }

        destroy_buffers(it->target);

        it = m_erased.erase(it);
    }
}

}
