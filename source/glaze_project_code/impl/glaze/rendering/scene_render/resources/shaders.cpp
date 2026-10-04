#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/defines.h"

#include "glp/glaze/utilities/core/assert.h"

#include <unordered_map>

namespace glaze::rendering::scene_render::resources::shaders
{

namespace
{

struct replaced_shader
{
    shader target;
    int frames_left = 0;
};

std::unordered_map<std::string, shader> m_shaders;

std::vector<replaced_shader> m_replaced;

void destroy_pipelines(const shader& target)
{
    vk::shaders::destroy_pipeline(target.graphics);
    vk::shaders::destroy_pipeline(target.compute);
}

}

const shader* build(
    const std::string& name,
    const std::string& vert_file,
    const std::string& frag_file,
    const std::string& comp_file,
    const graphics_pipeline_create_info& graphics_create_info,
    const compute_pipeline_create_info& compute_create_info
)
{
    GLAZE_ASSERT(vert_file.empty() && comp_file.empty(), "shader has no vertex or compute shader: " + name);

    shader result;

    if (!vert_file.empty()) {
        result.graphics = vk::shaders::create_graphics_pipeline(vert_file, frag_file, graphics_create_info);
    }

    if (!comp_file.empty()) {
        result.compute = vk::shaders::create_compute_pipeline(comp_file, compute_create_info);
    }

    const auto found = m_shaders.find(name);

    if (found == m_shaders.end()) {
        return &m_shaders.emplace(name, result).first->second;
    }

    m_replaced.push_back({ found->second, MAX_FRAMES_IN_FLIGHT });

    found->second = result;

    return &found->second;
}

const shader* get(const std::string& name)
{
    const auto found = m_shaders.find(name);

    return found != m_shaders.end() ? &found->second : nullptr;
}

std::vector<std::string> get_all_names()
{
    std::vector<std::string> names;

    names.reserve(m_shaders.size());

    for (const auto& entry : m_shaders) {
        names.push_back(entry.first);
    }

    return names;
}

void erase_all()
{
    for (const auto& entry : m_shaders) {
        destroy_pipelines(entry.second);
    }

    for (const replaced_shader& replaced : m_replaced) {
        destroy_pipelines(replaced.target);
    }

    m_shaders.clear();
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
