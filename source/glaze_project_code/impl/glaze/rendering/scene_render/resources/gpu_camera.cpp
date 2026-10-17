#include "glp/glaze/rendering/scene_render/resources/gpu_camera.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>

namespace glaze::rendering::scene_render::res::gpu_cameras
{

namespace
{

void place(gpu_camera& target, const glm::mat4& local)
{
    target.view     = glm::inverse(local);
    target.position = glm::vec3(local[3]);
}

void finish(gpu_camera& target)
{
    target.view_proj     = target.proj * target.view;
    target.inv_view_proj = glm::inverse(target.view_proj);
}

}

gpu_camera build_perspective(float fov, float aspect, float near_plane, float far_plane, const glm::mat4& local)
{
    gpu_camera result;

    place(result, local);

    result.proj = glm::perspectiveRH_ZO(glm::radians(fov), aspect, far_plane, near_plane);
    result.proj[1][1] *= -1.0f;

    finish(result);

    return result;
}

gpu_camera build_ortho(float ortho_size, float aspect, float near_plane, float far_plane, const glm::mat4& local)
{
    gpu_camera result;

    place(result, local);

    const float half_height = ortho_size;
    const float half_width  = ortho_size * aspect;

    result.proj = glm::orthoRH_ZO(-half_width, half_width, -half_height, half_height, far_plane, near_plane);
    result.proj[1][1] *= -1.0f;

    finish(result);

    return result;
}

}
