#include "glp/trainer/objects/field_editors.h"

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include <imgui.h>

namespace trainer::objects
{

bool edit_float(const char* label, float& value, float speed, float min, float max)
{
    return ImGui::DragFloat(ui::property::begin_row(label).c_str(), &value, speed, min, max);
}

bool edit_vec2(const char* label, glm::vec2& value, float speed)
{
    return ImGui::DragFloat2(ui::property::begin_row(label).c_str(), &value.x, speed);
}

bool edit_vec3(const char* label, glm::vec3& value, float speed)
{
    return ImGui::DragFloat3(ui::property::begin_row(label).c_str(), &value.x, speed, 0.0f, 0.0f, "%.6f");
}

bool edit_color(const char* label, glm::vec3& value)
{
    return ImGui::ColorEdit3(ui::property::begin_row(label).c_str(), &value.x, ImGuiColorEditFlags_Float);
}

bool edit_uint(const char* label, uint32_t& value, uint32_t min, uint32_t max)
{
    return ImGui::DragScalar(ui::property::begin_row(label).c_str(), ImGuiDataType_U32, &value, 1.0f, &min, &max);
}

bool edit_bool(const char* label, bool& value)
{
    return ImGui::Checkbox(ui::property::begin_row(label).c_str(), &value);
}

}

#endif
