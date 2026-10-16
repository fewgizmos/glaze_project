#ifndef NDEBUG

#include "glp/editor/scene/debug_grid.h"

#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace editor::scene
{

namespace
{

const glaze::rendering::scene_render::shader* debug_grid_shader()
{
    static const glaze::rendering::scene_render::shader* shd = [] {
        glaze::rendering::graphics_pipeline_create_info info;
        info.color_format         = glaze::rendering::vk::frame::get_swapchain().image_format;
        info.depth_format         = glaze::rendering::DEPTH_FORMAT;
        info.cull_mode            = VK_CULL_MODE_NONE;
        info.depth_write          = false;
        info.alpha_blend          = true;
        info.set_layouts          = { glaze::rendering::scene_render::it::get_scene_set_layout() };
        info.push_constant_size   = sizeof(VkDeviceAddress);
        info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        return glaze::rendering::scene_render::res::shaders::build("debug_grid", "debug_grid.vert", "debug_grid.frag", "", info, {}, glaze::rendering::scene_render::render_queue::blended);
    }();

    return shd;
}

}

cmp_debug_grid::cmp_debug_grid()
{
    set_flag(glaze::scene::vol::component_flags::modeled);

    const std::vector<glaze::rendering::scene_render::vertex> vertices(4, glaze::rendering::scene_render::vertex{});
    const std::vector<uint32_t> indices = { 0, 1, 2, 2, 1, 3 };

    m_mesh = glaze::rendering::scene_render::res::meshes::build(vertices, indices);

    glaze::rendering::scene_render::material_create_info create_info;
    create_info.target_shader = debug_grid_shader();

    m_material = glaze::rendering::scene_render::res::materials::build(create_info);
}

cmp_debug_grid::~cmp_debug_grid()
{
    glaze::rendering::scene_render::res::materials::erase(m_material);
    glaze::rendering::scene_render::res::meshes::erase(m_mesh);
}

glaze::rendering::scene_render::gpu_model cmp_debug_grid::get_model()
{
    glaze::rendering::scene_render::gpu_model result;

    result.parts.push_back({ m_mesh, m_material, glm::mat4(1.0f) });

    return result;
}

debug_grid_object::debug_grid_object()
{
    m_scene_gizmo_volume().components.push_back(std::make_unique<cmp_debug_grid>());
}

}

#endif
