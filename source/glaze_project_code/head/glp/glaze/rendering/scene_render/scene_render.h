#pragma once

#include "glp/glaze/rendering/vk/resources.h"

#include <glm/mat4x4.hpp>

namespace glaze::rendering::scene_render
{

void init();
void cleanup();

void update_all();
void draw_all();

const image& get_depth_image();

glm::mat4 get_view_proj();

}
