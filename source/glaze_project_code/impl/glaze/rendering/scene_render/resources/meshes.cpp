#include "glp/glaze/rendering/scene_render/resources/meshes.h"

#include "glp/glaze/rendering/defines.h"

#include <glm/common.hpp>

#include <limits>
#include <memory>
#include <unordered_map>

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

void destroy_buffers(mesh& target)
{
    vk::resources::destroy_buffer(target.index_buffer);
    vk::resources::destroy_buffer(target.vertex_buffer);
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

    created->vertex_buffer = vk::resources::create_buffer(
        vertices_size,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    );

    created->index_buffer = vk::resources::create_buffer(
        indices_size,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    );

    vk::resources::queue_upload(created->vertex_buffer, vertices.data(), vertices_size);
    vk::resources::queue_upload(created->index_buffer, indices.data(), indices_size);

    const mesh* result = created.get();

    m_meshes.emplace(result, std::move(created));

    return result;
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
