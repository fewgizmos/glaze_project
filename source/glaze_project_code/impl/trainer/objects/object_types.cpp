#include "glp/trainer/objects/object_types.h"

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include <string>
#include <utility>
#include <vector>

namespace trainer::objects
{

namespace
{

constexpr const char* UNKNOWN_TYPE_NAME = "scene_entity";

std::string m_last_error;

std::vector<object_type>& registry()
{
    static std::vector<object_type> instance;

    return instance;
}

}

void register_type(object_type type)
{
    registry().push_back(std::move(type));
}

const std::vector<object_type>& get_types()
{
    return registry();
}

const object_type* find_type(glaze::scene::scene_entity* entity)
{
    if (entity == nullptr) {
        return nullptr;
    }

    for (const object_type& type : get_types()) {
        if (type.matches && type.matches(entity)) {
            return &type;
        }
    }

    return nullptr;
}

std::string get_entity_name(glaze::scene::scene_entity* entity)
{
    const object_type* type = find_type(entity);

    return std::string(type != nullptr ? type->name : UNKNOWN_TYPE_NAME) + "_" + std::to_string(entity->get_id());
}

void report_error(std::string text)
{
    m_last_error = std::move(text);
}

const std::string& get_last_error()
{
    return m_last_error;
}

void clear_last_error()
{
    m_last_error.clear();
}

}

#endif
