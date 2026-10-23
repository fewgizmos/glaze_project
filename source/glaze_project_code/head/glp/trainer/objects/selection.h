#pragma once

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include "glp/glaze/scene/scene.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>

namespace trainer::objects::selection
{

struct transform_axis_prefs
{
    float step;
    bool  snap;
};

transform_axis_prefs get_move_prefs();
transform_axis_prefs get_rotate_prefs();
transform_axis_prefs get_scale_prefs();

void update();
void draw();

void select(uint64_t entity_id);
void clear();

glaze::scene::scene_entity* get_selected();

void request_spawn(const std::function<glaze::scene::scene_entity*(glm::vec3)>& create);

}

#endif
