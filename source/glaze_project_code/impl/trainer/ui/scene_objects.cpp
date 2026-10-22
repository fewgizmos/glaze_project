#ifndef NDEBUG

#include "glp/trainer/ui/scene_objects.h"

#include "glp/trainer/objects/object_types.h"
#include "glp/trainer/objects/selection.h"

#include "glp/glaze/scene/scene.h"

#include <imgui.h>

#include <glm/vec3.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace trainer::ui::scene_objects
{

namespace
{

namespace objects = trainer::objects;

constexpr const char* WINDOW_TITLE  = "Scene Objects";
constexpr const char* DELETE_POPUP  = "Delete object?";
constexpr const char* INACTIVE_MARK = " (inactive)";

constexpr float TRANSFORM_SPEED = 0.05f;
constexpr float ROTATION_SPEED  = 0.5f;
constexpr float SCALE_SPEED     = 0.01f;

const ImVec2 WINDOW_SIZE = ImVec2(420.0f, 560.0f);

const ImVec4 ERROR_COLOR = ImVec4(0.95f, 0.40f, 0.35f, 1.0f);

bool m_open = false;

bool m_focus_service = false;

int m_create_type = 0;

uint64_t m_last_selected = 0;

std::vector<const objects::object_type*> creatable_types()
{
    std::vector<const objects::object_type*> result;

    for (const objects::object_type& type : objects::get_types()) {
        if (type.is_creatable) {
            result.push_back(&type);
        }
    }

    return result;
}

void draw_error()
{
    if (!objects::get_last_error().empty()) {
        ImGui::TextColored(ERROR_COLOR, "%s", objects::get_last_error().c_str());
    }
}

void draw_entity_row(glaze::scene::scene_entity* entity, bool is_inactive)
{
    glaze::scene::scene_entity* selected = objects::selection::get_selected();

    const std::string label = objects::get_entity_name(entity) + (is_inactive ? INACTIVE_MARK : "");

    if (ImGui::Selectable(label.c_str(), entity == selected)) {
        objects::selection::select(entity->get_id());

        m_focus_service = true;
    }
}

void draw_browser()
{
    const std::vector<std::unique_ptr<glaze::scene::scene_entity>>& entities = glaze::scene::it::get_all_entities();

    std::vector<const objects::object_type*> groups;

    for (const objects::object_type& type : objects::get_types()) {
        groups.push_back(&type);
    }

    groups.push_back(nullptr);

    for (const objects::object_type* group : groups) {
        std::vector<glaze::scene::scene_entity*> members;

        for (const std::unique_ptr<glaze::scene::scene_entity>& entity : entities) {
            if (!glaze::scene::it::util::is_entity_pending_erase(entity.get()) && objects::find_type(entity.get()) == group) {
                members.push_back(entity.get());
            }
        }

        if (members.empty()) {
            continue;
        }

        const std::string header = std::string(group != nullptr ? group->name : "other") + " (" + std::to_string(members.size()) + ")";

        if (!ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            continue;
        }

        for (size_t i = 0; i < members.size(); i++) {
            draw_entity_row(members[i], group != nullptr && group->only_first_active && i > 0);
        }

        ImGui::TreePop();
    }
}

void draw_transform(glaze::scene::scene_entity* entity)
{
    glm::vec3 position = entity->get_position();
    glm::vec3 rotation = entity->get_rotation();
    glm::vec3 scale    = entity->get_scale();

    bool changed = false;

    changed |= ImGui::DragFloat3("position", &position.x, TRANSFORM_SPEED);
    changed |= ImGui::DragFloat3("rotation", &rotation.x, ROTATION_SPEED);
    changed |= ImGui::DragFloat3("scale", &scale.x, SCALE_SPEED);

    if (changed) {
        entity->set_transform(position, rotation, scale);
    }
}

void draw_delete(glaze::scene::scene_entity* entity)
{
    const bool is_player = entity == glaze::scene::it::get_player_entity();

    ImGui::BeginDisabled(is_player);

    if (ImGui::Button("Delete")) {
        ImGui::OpenPopup(DELETE_POPUP);
    }

    ImGui::EndDisabled();

    if (is_player) {
        ImGui::SameLine();
        ImGui::TextDisabled("the active camera cannot be deleted");
    }

    if (!ImGui::BeginPopupModal(DELETE_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::Text("%s", objects::get_entity_name(entity).c_str());

    if (ImGui::Button("Yes")) {
        glaze::scene::it::erase_entity(entity);

        objects::selection::clear();

        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button("No")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void draw_service()
{
    glaze::scene::scene_entity* entity = objects::selection::get_selected();

    if (entity == nullptr) {
        ImGui::TextDisabled("nothing selected");
        return;
    }

    if (entity->get_id() != m_last_selected) {
        m_last_selected = entity->get_id();

        objects::clear_last_error();
    }

    const objects::object_type* type = objects::find_type(entity);

    ImGui::SeparatorText(objects::get_entity_name(entity).c_str());

    if (type == nullptr || type->has_transform) {
        draw_transform(entity);
    }

    if (type != nullptr && type->draw_fields) {
        ImGui::Separator();

        type->draw_fields(entity);
    }

    draw_error();

    ImGui::Separator();

    draw_delete(entity);
}

void draw_create()
{
    const std::vector<const objects::object_type*> types = creatable_types();

    if (types.empty()) {
        return;
    }

    m_create_type = std::min(m_create_type, static_cast<int>(types.size()) - 1);

    if (ImGui::BeginCombo("type", types[m_create_type]->name)) {
        for (int i = 0; i < static_cast<int>(types.size()); i++) {
            if (ImGui::Selectable(types[i]->name, i == m_create_type)) {
                m_create_type = i;
            }
        }

        ImGui::EndCombo();
    }

    const objects::object_type* type = types[m_create_type];

    ImGui::Separator();

    if (type->draw_draft) {
        type->draw_draft();
    }

    ImGui::Separator();

    if (ImGui::Button("Create")) {
        objects::clear_last_error();

        objects::selection::request_spawn(type->create);
    }

    draw_error();
}

}

void draw_menu()
{
    ImGui::MenuItem(WINDOW_TITLE, nullptr, &m_open);
}

void draw()
{
    if (!m_open) {
        return;
    }

    ImGui::SetNextWindowSize(WINDOW_SIZE, ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(WINDOW_TITLE, &m_open)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("scene_objects_tabs")) {
        if (ImGui::BeginTabItem("Browser")) {
            draw_browser();
            ImGui::EndTabItem();
        }

        const ImGuiTabItemFlags service_flags = m_focus_service ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;

        m_focus_service = false;

        if (ImGui::BeginTabItem("Service", nullptr, service_flags)) {
            draw_service();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Create")) {
            draw_create();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

}

#endif
