#ifndef NDEBUG

#include "glp/trainer/objects/field_editors.h"
#include "glp/trainer/objects/object_types.h"

#include "glp/glaze/scene/objects/light_object.h"
#include "glp/glaze/scene/volume/cmp_light.h"

#include <array>
#include <cfloat>

namespace trainer::objects
{

namespace
{

namespace component = glaze::scene::vol::component;

constexpr float SPEED_SMALL  = 0.01f;
constexpr float SPEED_MEDIUM = 0.1f;
constexpr float SPEED_LARGE  = 1.0f;

constexpr float MAX_ANGLE     = 360.0f;
constexpr float MAX_ELEVATION = 90.0f;

const std::array<const char*, 3> LIGHT_TYPES = { "directional", "point", "ambient" };

component::light_create_info m_light_draft;

bool edit_light(component::light_create_info& info)
{
    bool changed = false;

    changed |= edit_enum("type", info.type, LIGHT_TYPES);
    changed |= edit_color("color", info.color);
    changed |= edit_color("ground color", info.ground_color);
    changed |= edit_float("intensity", info.intensity, SPEED_MEDIUM, 0.0f, FLT_MAX);
    changed |= edit_float("range", info.range, SPEED_MEDIUM, 0.0f, FLT_MAX);
    changed |= edit_float("cone angle", info.cone_angle, SPEED_LARGE, 0.0f, MAX_ANGLE);
    changed |= edit_float("cone softness", info.cone_softness, SPEED_SMALL, 0.0f, 1.0f);
    changed |= edit_float("angular radius", info.angular_radius, SPEED_SMALL, 0.0f, MAX_ELEVATION);

    return changed;
}

void draw_light_fields(glaze::scene::scene_entity* entity)
{
    component::light* target = find_component<component::light>(entity);

    if (target == nullptr) {
        return;
    }

    component::light_create_info info = target->get_create_info();

    if (edit_light(info)) {
        target->set_create_info(info);
    }
}

const bool registered = [] {
    object_type type;
    type.name         = "light_object_prim";
    type.label        = "Light";
    type.is_creatable = true;
    type.matches      = is_type<glaze::scene::obj::light_object_prim>;
    type.draw_fields  = draw_light_fields;
    type.draw_draft   = [] { edit_light(m_light_draft); };
    type.create       = [](glm::vec3 position) -> glaze::scene::scene_entity* {
        return glaze::scene::it::create_entity<glaze::scene::obj::light_object_prim>(position, glm::vec3(0.0f), m_light_draft);
    };

    register_type(type);

    return true;
}();

}

}

#endif
