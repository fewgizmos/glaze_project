#ifndef NDEBUG

#include "glp/trainer/objects/field_editors.h"
#include "glp/trainer/objects/object_types.h"
#include "glp/trainer/scene/debug_grid.h"

namespace trainer::objects
{

namespace
{

const bool registered = [] {
    object_type type;
    type.name          = "debug_grid_object";
    type.label         = "Debug Grid";
    type.has_transform = false;
    type.matches       = is_type<trainer::scene::debug_grid_object>;

    register_type(type);

    return true;
}();

}

}

#endif
