#include "glp/glaze/rendering/resources/material.h"

#include "glp/glaze/rendering/queue_deletor.h"

#include "glp/glaze/utilities/core/assert.h"

#include <unordered_map>

namespace glaze::rendering::resources::materials
{

namespace
{

std::unordered_map<std::string, material> m_materials;

}

const material* build(
    const std::string& name,
    const std::string& vert_file,
    const std::string& frag_file,
    const std::string& comp_file,
    const graphics_pipeline_create_info& graphics_create_info,
    const compute_pipeline_create_info& compute_create_info
)
{
    GLAZE_ASSERT(vert_file.empty() && comp_file.empty(), "material has no vertex or compute shader: " + name);

    material result;

    if (!vert_file.empty()) {
        result.graphics = vk::shaders::create_graphics_pipeline(vert_file, frag_file, graphics_create_info);
    }

    if (!comp_file.empty()) {
        result.compute = vk::shaders::create_compute_pipeline(comp_file, compute_create_info);
    }

    queue_deletor::push([name, result]() {
        vk::shaders::destroy_pipeline(result.graphics);
        vk::shaders::destroy_pipeline(result.compute);

        m_materials.erase(name);
    });

    return &m_materials.try_emplace(name, result).first->second;
}

const material* get(const std::string& name)
{
    const auto found = m_materials.find(name);

    return found != m_materials.end() ? &found->second : nullptr;
}

std::vector<std::string> get_all_names()
{
    std::vector<std::string> names;

    names.reserve(m_materials.size());

    for (const auto& entry : m_materials) {
        names.push_back(entry.first);
    }

    return names;
}

}
