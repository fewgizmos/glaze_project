#ifndef NDEBUG

#include "glp/glaze/rendering/scene_render/debug_gizmos.h"

#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/material.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>

namespace glaze::rendering::scene_render::debug_gizmos
{

namespace
{

struct debug_grid_push
{
    glm::mat4 view_proj;
    glm::mat4 inv_view_proj;
};

const material* m_debug_grid_material = nullptr;

void create_pipelines()
{
    graphics_pipeline_create_info debug_grid_create_info;
    debug_grid_create_info.color_format = vk::frame::get_swapchain().image_format;
    debug_grid_create_info.depth_format = DEPTH_FORMAT;
    debug_grid_create_info.cull_mode = VK_CULL_MODE_NONE;
    debug_grid_create_info.depth_write = false;
    debug_grid_create_info.alpha_blend = true;
    debug_grid_create_info.push_constant_size = sizeof(debug_grid_push);
    debug_grid_create_info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    m_debug_grid_material = resources::materials::build(
        "debug_grid",
        "debug_grid.vert",
        "debug_grid.frag",
        "",
        debug_grid_create_info,
        {}
    );
}

void draw_grid()
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const glm::mat4 view_proj = scene_render::get_view_proj();

    disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debug_grid_material->graphics.handle);

    const debug_grid_push push = { view_proj, glm::inverse(view_proj) };

    vk::commands::push_constants(
        cmd,
        m_debug_grid_material->graphics.layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        sizeof(push),
        &push
    );

    disp.cmdDraw(cmd, 3, 1, 0, 0);
}

}

void init_all()
{
    create_pipelines();
}

void clean_all()
{
    m_debug_grid_material = nullptr;
}

void draw_all()
{
    draw_grid();
}

}

#endif
