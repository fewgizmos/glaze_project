#ifndef NDEBUG

#include "glp/trainer/objects/field_editors.h"
#include "glp/trainer/objects/object_types.h"

#include "glp/glaze/world/resources/skybox.h"

namespace trainer::objects
{

namespace
{

const bool registered = [] {
    object_type type;
    type.name          = "skybox";
    type.label         = "Skybox";
    type.has_transform = false;
    type.matches       = is_type<glaze::world::res::skybox>;

    register_type(type);

    return true;
}();

}

}

#endif
