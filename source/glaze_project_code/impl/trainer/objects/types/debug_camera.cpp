#ifndef NDEBUG

#include "glp/trainer/objects/field_editors.h"
#include "glp/trainer/objects/object_types.h"
#include "glp/trainer/scene/debug_camera.h"

#include <cfloat>

namespace trainer::objects
{

namespace
{

constexpr float SPEED_MEDIUM = 0.1f;

void draw_camera_fields(glaze::scene::scene_entity* entity)
{
    trainer::scene::debug_camera_player_obj* target = dynamic_cast<trainer::scene::debug_camera_player_obj*>(entity);

    if (target == nullptr) {
        return;
    }

    float speed = target->get_speed();

    if (edit_float("speed", speed, SPEED_MEDIUM, 0.0f, FLT_MAX)) {
        target->set_speed(speed);
    }
}

const bool registered = [] {
    object_type type;
    type.name        = "debug_camera_player_obj";
    type.label       = "Debug Camera";
    type.matches     = is_type<trainer::scene::debug_camera_player_obj>;
    type.draw_fields = draw_camera_fields;

    register_type(type);

    return true;
}();

}

}

#endif
