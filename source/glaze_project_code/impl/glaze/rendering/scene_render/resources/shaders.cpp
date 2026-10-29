#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/scene_render/environment.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/defines.h"

#include "glp/glaze/utilities/core/assert.h"

#include <exception>
#include <iostream>
#include <unordered_map>

namespace glaze::rendering::scene_render::res::shaders
{

namespace
{

struct shader_source
{
    std::string vert_file;
    std::string frag_file;
    std::string comp_file;

    graphics_pipeline_create_info graphics_create_info;
    compute_pipeline_create_info compute_create_info;

    render_queue queue = render_queue::opaque;
};

struct shader_entry
{
    shader target;
    shader_source source;
};

struct replaced_shader
{
    shader target;
    int frames_left = 0;
};

std::unordered_map<std::string, shader_entry> m_shaders;

std::vector<replaced_shader> m_replaced;

void destroy_pipelines(const shader& target)
{
    vk::shaders::destroy_pipeline(target.graphics);
    vk::shaders::destroy_pipeline(target.compute);
}

shader create_shader(const std::string& name, const shader_source& source)
{
    GLAZE_ASSERT(source.vert_file.empty() && source.comp_file.empty(), "shader has no vertex or compute shader: " + name);

    shader result;

    result.queue = source.queue;

    if (!source.vert_file.empty()) {
        result.graphics = vk::shaders::create_graphics_pipeline(source.vert_file, source.frag_file, source.graphics_create_info);

        result.uses_environment = source.graphics_create_info.set_layouts.size() > 1
                               && source.graphics_create_info.set_layouts[1] == environment::get_set_layout();
    }

    if (!source.comp_file.empty()) {
        try {
            result.compute = vk::shaders::create_compute_pipeline(source.comp_file, source.compute_create_info);
        }

        catch (...) {
            vk::shaders::destroy_pipeline(result.graphics);
            throw;
        }
    }

    return result;
}

}

const shader* build(
    const std::string& name,
    const std::string& vert_file,
    const std::string& frag_file,
    const std::string& comp_file,
    const graphics_pipeline_create_info& graphics_create_info,
    const compute_pipeline_create_info& compute_create_info,
    render_queue queue
)
{
    const shader_source source = { vert_file, frag_file, comp_file, graphics_create_info, compute_create_info, queue };

    const shader result = create_shader(name, source);

    const auto found = m_shaders.find(name);

    if (found == m_shaders.end()) {
        return &m_shaders.emplace(name, shader_entry{ result, source }).first->second.target;
    }

    m_replaced.push_back({ found->second.target, MAX_FRAMES_IN_FLIGHT });

    found->second = { result, source };

    return &found->second.target;
}

const shader* get(const std::string& name)
{
    const auto found = m_shaders.find(name);

    return found != m_shaders.end() ? &found->second.target : nullptr;
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

uint32_t reload_all()
{
    uint32_t failed = 0;

    for (auto& [name, entry] : m_shaders) {
        try {
            const shader result = create_shader(name, entry.source);

            m_replaced.push_back({ entry.target, MAX_FRAMES_IN_FLIGHT });

            entry.target = result;
        }

        catch (const std::exception& error) {
            std::cerr << error.what() << "\n";

            failed++;
        }
    }

    return failed;
}

void erase_all()
{
    for (const auto& entry : m_shaders) {
        destroy_pipelines(entry.second.target);
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

namespace glaze::rendering::scene_render::res::shaders::stdlib
{

namespace
{

const shader* build_base_model(const std::string& name, VkCullModeFlags cull_mode, bool masked)
{
    graphics_pipeline_create_info info;
    info.color_formats        = { HDR_FORMAT };
    info.depth_format         = DEPTH_FORMAT;
    info.depth_compare        = masked ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_GREATER_OR_EQUAL;
    info.depth_write          = !masked;
    info.cull_mode            = cull_mode;
    info.front_face           = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    info.set_layouts          = { it::get_scene_set_layout(), environment::get_set_layout() };
    info.push_constant_size   = sizeof(VkDeviceAddress);
    info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    if (masked) {
        info.defines = { "DEPTH_EQUAL_PASS" };
    }

    return build(name, "materials/base_model.vert", "materials/base_model.frag", "", info, {}, render_queue::opaque);
}

const shader* build_prepass(const std::string& name, VkCullModeFlags cull_mode)
{
    graphics_pipeline_create_info info;
    info.color_formats        = { GBUFFER_NORMAL_FORMAT, GBUFFER_VELOCITY_FORMAT };
    info.depth_format         = DEPTH_FORMAT;
    info.cull_mode            = cull_mode;
    info.front_face           = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    info.set_layouts          = { it::get_scene_set_layout() };
    info.push_constant_size   = sizeof(VkDeviceAddress);
    info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    info.defines              = { "PREPASS" };

    return build(name, "materials/base_model.vert", "sys/frame/prepass.frag", "", info, {}, render_queue::opaque);
}

}

const shader* base_model_shader()
{
    static const shader* shd = build_base_model("base_model", VK_CULL_MODE_BACK_BIT, false);

    return shd;
}

const shader* base_model_double_sided_shader()
{
    static const shader* shd = build_base_model("base_model_double_sided", VK_CULL_MODE_NONE, false);

    return shd;
}

const shader* base_model_masked_shader()
{
    static const shader* shd = build_base_model("base_model_masked", VK_CULL_MODE_BACK_BIT, true);

    return shd;
}

const shader* base_model_masked_double_sided_shader()
{
    static const shader* shd = build_base_model("base_model_masked_double_sided", VK_CULL_MODE_NONE, true);

    return shd;
}

const shader* prepass_shader()
{
    static const shader* shd = build_prepass("prepass", VK_CULL_MODE_BACK_BIT);

    return shd;
}

const shader* prepass_double_sided_shader()
{
    static const shader* shd = build_prepass("prepass_double_sided", VK_CULL_MODE_NONE);

    return shd;
}

}
