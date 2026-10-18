#include "glp/glaze/rendering/scene_render/resources/gpu_model.h"

namespace glaze::rendering::scene_render::res::gpu_models
{

namespace
{

std::vector<glm::mat4> m_node_matrices;

}

void build(const model* target, const glm::mat4& local, gpu_model& out)
{
    if (target == nullptr) {
        return;
    }

    m_node_matrices.resize(target->nodes.size());

    for (size_t i = 0; i < target->nodes.size(); i++) {
        const model_node& node = target->nodes[i];

        m_node_matrices[i] = (node.parent < 0 ? local : m_node_matrices[node.parent]) * node.local;

        for (const model_part& part : node.parts) {
            out.parts.push_back({ part.target_mesh, part.target_material, m_node_matrices[i], m_node_matrices[i] });
        }
    }
}

}
