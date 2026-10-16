#include "glp/glaze/rendering/scene_render/resources/shaders_stdlib.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"

#include <string>

namespace glaze::rendering::scene_render
{

namespace
{

const shader* build_base_model(const std::string& name, VkCullModeFlags cull_mode)
{
    graphics_pipeline_create_info info;
    info.color_format         = vk::frame::get_swapchain().image_format;
    info.depth_format         = DEPTH_FORMAT;
    info.cull_mode            = cull_mode;
    info.front_face           = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    info.set_layouts          = { it::get_scene_set_layout() };
    info.push_constant_size   = sizeof(VkDeviceAddress);
    info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    return res::shaders::build(name, "base_model.vert", "base_model.frag", "", info, {}, render_queue::opaque);
}

}

const shader* std_base_model_shader()
{
    static const shader* shd = build_base_model("base_model", VK_CULL_MODE_BACK_BIT);

    return shd;
}

const shader* std_base_model_double_sided_shader()
{
    static const shader* shd = build_base_model("base_model_double_sided", VK_CULL_MODE_NONE);

    return shd;
}

}
