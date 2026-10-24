#pragma once

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include "glp/trainer/ui/property.h"

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/volume/volume.h"

#include <imgui.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace trainer::objects
{

template<typename component_type>
component_type* find_component(glaze::scene::scene_entity* entity)
{
    for (const std::unique_ptr<glaze::scene::vol::volume_component>& target : entity->scene_volume().components) {
        if (component_type* found = dynamic_cast<component_type*>(target.get())) {
            return found;
        }
    }

    return nullptr;
}

template<typename entity_type>
bool is_type(glaze::scene::scene_entity* entity)
{
    return dynamic_cast<entity_type*>(entity) != nullptr;
}

bool edit_float(const char* label, float& value, float speed, float min = 0.0f, float max = 0.0f);
bool edit_vec2(const char* label, glm::vec2& value, float speed);
bool edit_vec3(const char* label, glm::vec3& value, float speed);
bool edit_color(const char* label, glm::vec3& value);
bool edit_uint(const char* label, uint32_t& value, uint32_t min, uint32_t max);
bool edit_bool(const char* label, bool& value);

template<typename enum_type, std::size_t count>
bool edit_enum(const char* label, enum_type& value, const std::array<const char*, count>& names)
{
    int index = static_cast<int>(value);

    if (!ImGui::Combo(ui::property::begin_row(label).c_str(), &index, names.data(), static_cast<int>(count))) {
        return false;
    }

    value = static_cast<enum_type>(index);

    return true;
}

template<typename info_type>
struct info_field
{
    const char* name;
    const char* group;
    std::function<bool(const char*, info_type&)> edit;
};

#define INFO_FIELD(member, editor) \
    { #member, nullptr, [edit = editor](const char* label, auto& info) { return edit(label, info.member); } }

#define INFO_FIELD_IN(group_name, member, editor) \
    { #member, group_name, [edit = editor](const char* label, auto& info) { return edit(label, info.member); } }

inline auto float_editor(float speed, float min = 0.0f, float max = 0.0f)
{
    return [=](const char* label, float& value) { return edit_float(label, value, speed, min, max); };
}

inline auto vec2_editor(float speed)
{
    return [=](const char* label, glm::vec2& value) { return edit_vec2(label, value, speed); };
}

inline auto vec3_editor(float speed)
{
    return [=](const char* label, glm::vec3& value) { return edit_vec3(label, value, speed); };
}

inline auto color_editor()
{
    return [](const char* label, glm::vec3& value) { return edit_color(label, value); };
}

inline auto uint_editor(uint32_t min, uint32_t max)
{
    return [=](const char* label, uint32_t& value) { return edit_uint(label, value, min, max); };
}

inline auto bool_editor()
{
    return [](const char* label, bool& value) { return edit_bool(label, value); };
}

template<std::size_t count>
auto enum_editor(const std::array<const char*, count>& names)
{
    return [&names](const char* label, auto& value) { return edit_enum(label, value, names); };
}

template<typename info_type>
bool edit_fields(const std::vector<info_field<info_type>>& fields, info_type& info)
{
    bool changed = false;

    for (const info_field<info_type>& field : fields) {
        if (field.group == nullptr) {
            changed |= field.edit(field.name, info);
        }
    }

    std::vector<const char*> groups;

    for (const info_field<info_type>& field : fields) {
        if (field.group == nullptr) {
            continue;
        }

        const auto found = std::find_if(groups.begin(), groups.end(), [&](const char* existing) {
            return std::strcmp(existing, field.group) == 0;
        });

        if (found == groups.end()) {
            groups.push_back(field.group);
        }
    }

    for (const char* group : groups) {
        if (!ui::property::begin_category(group)) {
            continue;
        }

        for (const info_field<info_type>& field : fields) {
            if (field.group != nullptr && std::strcmp(field.group, group) == 0) {
                changed |= field.edit(field.name, info);
            }
        }
    }

    return changed;
}

}

#endif
