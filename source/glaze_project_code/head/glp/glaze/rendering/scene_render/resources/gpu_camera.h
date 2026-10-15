#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace glaze::rendering::scene_render
{

struct gpu_camera
{
    glm::mat4 view          = glm::mat4(1.0f);
    glm::mat4 proj          = glm::mat4(1.0f);
    glm::mat4 view_proj     = glm::mat4(1.0f);
    glm::mat4 inv_view_proj = glm::mat4(1.0f);

    glm::vec3 position = glm::vec3(0.0f);
};

}

namespace glaze::rendering::scene_render::res::gpu_cameras
{

gpu_camera build_perspective(float fov, float aspect, float near_plane, float far_plane, const glm::mat4& local);
gpu_camera build_ortho(float ortho_size, float aspect, float near_plane, float far_plane, const glm::mat4& local);

void apply_world(gpu_camera& target, const glm::mat4& world);

}
