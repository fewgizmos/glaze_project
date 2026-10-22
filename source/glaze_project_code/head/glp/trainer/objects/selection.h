#pragma once

#ifndef NDEBUG

#include "glp/glaze/scene/scene.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>

namespace trainer::objects::selection
{

void update();
void draw();

void select(uint64_t entity_id);
void clear();

glaze::scene::scene_entity* get_selected();

void request_spawn(const std::function<glaze::scene::scene_entity*(glm::vec3)>& create);

}

#endif
