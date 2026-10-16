#include "glp/glaze/rendering/scene_render/resources/gpu_light.h"

#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

namespace glaze::rendering::scene_render::res::gpu_lights
{

void apply_world(gpu_light& target, const glm::mat4& world)
{
    target.position  = glm::vec3(world * glm::vec4(target.position, 1.0f));
    target.direction = glm::normalize(glm::vec3(world * glm::vec4(target.direction, 0.0f)));
}

}
