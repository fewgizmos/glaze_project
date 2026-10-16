#include "glp/glaze/scene/volume/cmp_post.h"

namespace glaze::scene::vol::component
{

post::post(const post_create_info& create_info) : m_create_info(create_info)
{
    set_flag(component_flags::post);
}

rendering::scene_render::gpu_post post::get_post()
{
    rendering::scene_render::gpu_post result;

    result.exposure   = m_create_info.exposure;
    result.tonemapper = static_cast<uint32_t>(m_create_info.tonemapper);

    return result;
}

}
