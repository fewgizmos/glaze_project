#pragma once

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include "glp/glaze/scene/scene.h"

#include <glm/vec3.hpp>

#include <functional>
#include <string>
#include <vector>

namespace trainer::objects
{

struct object_type
{
    const char* name  = "";
    const char* label = "";

    bool has_transform     = true;
    bool is_creatable      = false;
    bool only_first_active = false;

    std::function<bool(glaze::scene::scene_entity*)> matches;
    std::function<void(glaze::scene::scene_entity*)> draw_fields;

    std::function<void()> draw_draft;
    std::function<glaze::scene::scene_entity*(glm::vec3)> create;
};

void register_type(object_type type);

const std::vector<object_type>& get_types();

const object_type* find_type(glaze::scene::scene_entity* entity);

std::string get_entity_name(glaze::scene::scene_entity* entity);

void report_error(std::string text);
const std::string& get_last_error();
void clear_last_error();

}

#endif
