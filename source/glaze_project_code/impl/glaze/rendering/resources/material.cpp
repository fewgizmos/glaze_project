#include "glp/glaze/rendering/resources/material.h"

#include "glp/glaze/rendering/defines.h"

#include "glp/glaze/utilities/core/assert.h"

#include <unordered_map>

namespace glaze::rendering::resources::materials
{

namespace
{

struct replaced_material
{
    material target;
    int frames_left = 0;
};

std::unordered_map<std::string, material> m_materials;

std::vector<replaced_material> m_replaced;

void destroy_pipelines(const material& target)
{
    vk::shaders::destroy_pipeline(target.graphics);
    vk::shaders::destroy_pipeline(target.compute);
}

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

    const auto found = m_materials.find(name);

    if (found == m_materials.end()) {
        return &m_materials.emplace(name, result).first->second;
    }

    m_replaced.push_back({ found->second, MAX_FRAMES_IN_FLIGHT });

    found->second = result;

    return &found->second;
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

void erase_all()
{
    for (const auto& entry : m_materials) {
        destroy_pipelines(entry.second);
    }

    for (const replaced_material& replaced : m_replaced) {
        destroy_pipelines(replaced.target);
    }

    m_materials.clear();
    m_replaced.clear();
}

void destroy_replaced()
{
    for (auto it = m_replaced.begin(); it != m_replaced.end();) {
        if (it->frames_left > 0) {
            it->frames_left--;
            it++;

            continue;
        }

        destroy_pipelines(it->target);

        it = m_replaced.erase(it);
    }
}

}
