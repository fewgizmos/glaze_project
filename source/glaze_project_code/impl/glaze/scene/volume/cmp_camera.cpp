#include "glp/glaze/scene/volume/cmp_camera.h"

#include "glp/glaze/io/viewport.h"

namespace glaze::scene::vol::component
{

camera::camera(camera_projection projection, float fov, float ortho_size, float near_plane, float far_plane)
    : m_projection(projection), m_fov(fov), m_ortho_size(ortho_size), m_near_plane(near_plane), m_far_plane(far_plane)
{
    set_flag(component_flags::camera);
}

rendering::scene_render::gpu_camera camera::get_camera()
{
    const glm::ivec2 extent = io::viewport::get_extent();

    if (extent.x == 0 || extent.y == 0) {
        return {};
    }

    const float aspect = static_cast<float>(extent.x) / static_cast<float>(extent.y);

    const glm::mat4 local = compose_transform(get_pos(), get_rot(), glm::vec3(1.0f));

    if (m_projection == camera_projection::orthographic) {
        return rendering::scene_render::res::gpu_cameras::build_ortho(m_ortho_size, aspect, m_near_plane, m_far_plane, local);
    }

    return rendering::scene_render::res::gpu_cameras::build_perspective(m_fov, aspect, m_near_plane, local);
}

}
