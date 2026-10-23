#include "glp/trainer/objects/field_editors.h"

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include "glp/trainer/objects/object_types.h"

#include "glp/glaze/scene/objects/static_camera_object.h"

namespace trainer::objects
{

namespace
{

const bool registered = [] {
    object_type type;
    type.name    = "static_camera_object_prim";
    type.label   = "Static Camera";
    type.matches = is_type<glaze::scene::obj::static_camera_object_prim>;

    register_type(type);

    return true;
}();

}

}

#endif
