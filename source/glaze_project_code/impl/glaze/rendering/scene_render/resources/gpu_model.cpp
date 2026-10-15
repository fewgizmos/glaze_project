#include "glp/glaze/rendering/scene_render/resources/gpu_model.h"

namespace glaze::rendering::scene_render::res::gpu_models
{

gpu_model build(const model* target, const glm::mat4& local)
{
    gpu_model result;

    if (target == nullptr) {
        return result;
    }

    std::vector<glm::mat4> node_matrices(target->nodes.size());

    for (size_t i = 0; i < target->nodes.size(); i++) {
        const model_node& node = target->nodes[i];

        node_matrices[i] = (node.parent < 0 ? local : node_matrices[node.parent]) * node.local;

        for (const model_part& part : node.parts) {
            result.parts.push_back({ part.target_mesh, part.target_material, node_matrices[i] });
        }
    }

    return result;
}

void apply_world(gpu_model& target, const glm::mat4& world)
{
    for (gpu_model_part& part : target.parts) {
        part.transform = world * part.transform;
    }
}

}
