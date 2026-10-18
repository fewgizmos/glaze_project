#include "glp/glaze/rendering/scene_render/resources/shaders_stdlib.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/scene_render/environment.h"

#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"

#include <string>

namespace glaze::rendering::scene_render
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

    return res::shaders::build(name, "materials/base_model.vert", "materials/base_model.frag", "", info, {}, render_queue::opaque);
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

    return res::shaders::build(name, "materials/base_model.vert", "sys/frame/prepass.frag", "", info, {}, render_queue::opaque);
}

}

const shader* std_base_model_shader()
{
    static const shader* shd = build_base_model("base_model", VK_CULL_MODE_BACK_BIT, false);

    return shd;
}

const shader* std_base_model_double_sided_shader()
{
    static const shader* shd = build_base_model("base_model_double_sided", VK_CULL_MODE_NONE, false);

    return shd;
}

const shader* std_base_model_masked_shader()
{
    static const shader* shd = build_base_model("base_model_masked", VK_CULL_MODE_BACK_BIT, true);

    return shd;
}

const shader* std_base_model_masked_double_sided_shader()
{
    static const shader* shd = build_base_model("base_model_masked_double_sided", VK_CULL_MODE_NONE, true);

    return shd;
}

const shader* std_prepass_shader()
{
    static const shader* shd = build_prepass("prepass", VK_CULL_MODE_BACK_BIT);

    return shd;
}

const shader* std_prepass_double_sided_shader()
{
    static const shader* shd = build_prepass("prepass_double_sided", VK_CULL_MODE_NONE);

    return shd;
}

}
