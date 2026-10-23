#include "glp/trainer/ui/scene_objects.h"

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include "glp/trainer/objects/object_types.h"
#include "glp/trainer/objects/selection.h"
#include "glp/trainer/ui/panels.h"

#include "glp/glaze/rendering/scene_render/frame_pipeline.h"

#include "glp/glaze/scene/scene.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

namespace trainer::ui::scene_objects
{

namespace
{

namespace objects = trainer::objects;

constexpr const char* BROWSER_TITLE  = "Objects Browser";
constexpr const char* DETAILS_TITLE  = "Object Details";
constexpr const char* ADD_POPUP      = "add_object";
constexpr const char* UNKNOWN_LABEL  = "Entity";
constexpr const char* ADD_LABEL      = "+ Create";
constexpr const char* TYPE_COLUMN_SAMPLE     = "Sky Atmosphere ";
constexpr const char* DISTANCE_COLUMN_SAMPLE = "000.00 km";
constexpr const char* ID_COLUMN_SAMPLE       = "0000000000";

constexpr uint64_t NO_ENTITY = 0;

constexpr int NO_CREATE_TYPE = -1;

constexpr float MOVE_DRAG_SPEED   = 0.05f;
constexpr float ROTATE_DRAG_SPEED = 0.5f;
constexpr float SCALE_DRAG_SPEED  = 0.01f;

constexpr float PANEL_MARGIN_X = 0.006f;
constexpr float PANEL_MARGIN_Y = 0.01f;
constexpr float PANEL_WIDTH    = 0.22f;
constexpr float BROWSER_SHARE  = 0.38f;

constexpr float BADGE_ROUNDING   = 0.25f;
constexpr float BADGE_SATURATION = 0.55f;
constexpr float BADGE_VALUE      = 0.92f;

constexpr ImU32 HUE_STEPS = 360;

constexpr float KILOMETER = 1000.0f;

constexpr float TRANSFORM_LABEL_WIDTH = 0.28f;

const ImVec4 ERROR_COLOR    = ImVec4(0.95f, 0.40f, 0.35f, 1.0f);
const ImVec4 INACTIVE_COLOR   = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
const ImVec4 ACTIVE_COLOR     = ImVec4(0.45f, 0.80f, 0.40f, 1.0f);
const ImVec4 BADGE_TEXT_COLOR = ImVec4(0.06f, 0.06f, 0.07f, 1.0f);

const ImVec4 CREATE_COLOR         = ImVec4(0.20f, 0.45f, 0.22f, 1.0f);
const ImVec4 CREATE_HOVERED_COLOR = ImVec4(0.26f, 0.58f, 0.28f, 1.0f);
const ImVec4 DELETE_COLOR         = ImVec4(0.55f, 0.18f, 0.16f, 1.0f);
const ImVec4 DELETE_HOVERED_COLOR = ImVec4(0.72f, 0.24f, 0.20f, 1.0f);

struct type_style
{
    const char* label;
    char glyph;
    ImVec4 color;
};

const std::array<ImVec4, 3> AXIS_COLORS = {
    ImVec4(0.80f, 0.22f, 0.20f, 1.0f),
    ImVec4(0.32f, 0.62f, 0.18f, 1.0f),
    ImVec4(0.20f, 0.40f, 0.85f, 1.0f)
};

const std::array<const char*, 3> AXIS_LABELS = { "X", "Y", "Z" };

bool m_browser_open = false;
bool m_details_open = false;

ImGuiTextFilter m_filter;

int m_create_type = NO_CREATE_TYPE;

uint64_t m_last_selected = NO_ENTITY;

bool m_scroll_to_selected  = false;
bool m_picked_from_browser = false;

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

void place_panel(float top_share, float height_share)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    const float margin_x = viewport->WorkSize.x * PANEL_MARGIN_X;
    const float margin_y = viewport->WorkSize.y * PANEL_MARGIN_Y;
    const float height   = viewport->WorkSize.y - margin_y * 3.0f;

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + margin_x, viewport->WorkPos.y + margin_y * (top_share > 0.0f ? 2.0f : 1.0f) + height * top_share), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * PANEL_WIDTH, height * height_share), ImGuiCond_FirstUseEver);
}

void track_selection()
{
    glaze::scene::scene_entity* selected = objects::selection::get_selected();

    const uint64_t selected_id = selected != nullptr ? selected->get_id() : NO_ENTITY;

    if (selected_id == m_last_selected) {
        return;
    }

    m_last_selected       = selected_id;
    m_scroll_to_selected  = !m_picked_from_browser;
    m_picked_from_browser = false;
    m_create_type         = NO_CREATE_TYPE;

    objects::clear_last_error();
}

void draw_error()
{
    if (!objects::get_last_error().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ERROR_COLOR);
        ImGui::TextWrapped("%s", objects::get_last_error().c_str());
        ImGui::PopStyleColor();
    }
}

void delete_entity(glaze::scene::scene_entity* entity)
{
    if (entity == objects::selection::get_selected()) {
        objects::selection::clear();
    }

    glaze::scene::it::erase_entity(entity);
}

type_style style_of(const objects::object_type* type)
{
    const char* label = type != nullptr && type->label[0] != '\0' ? type->label : UNKNOWN_LABEL;

    const ImU32 hue_hash = ImHashStr(type != nullptr ? type->name : UNKNOWN_LABEL);

    ImVec4 color(0.0f, 0.0f, 0.0f, 1.0f);

    ImGui::ColorConvertHSVtoRGB(static_cast<float>(hue_hash % HUE_STEPS) / static_cast<float>(HUE_STEPS), BADGE_SATURATION, BADGE_VALUE, color.x, color.y, color.z);

    return { label, static_cast<char>(std::toupper(static_cast<unsigned char>(label[0]))), color };
}

void draw_badge(const type_style& style, float size = 0.0f)
{
    if (size <= 0.0f) {
        size = ImGui::GetTextLineHeight();
    }

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max = ImVec2(min.x + size, min.y + size);

    const char glyph[2] = { style.glyph, '\0' };

    const ImVec2 glyph_size = ImGui::CalcTextSize(glyph);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(min, max, ImGui::GetColorU32(style.color), size * BADGE_ROUNDING);
    draw_list->AddText(ImVec2(min.x + (size - glyph_size.x) * 0.5f, min.y + (size - glyph_size.y) * 0.5f), ImGui::GetColorU32(BADGE_TEXT_COLOR), glyph);

    ImGui::Dummy(ImVec2(size, size));
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
}

bool has_transform(const objects::object_type* type)
{
    return type == nullptr || type->has_transform;
}

bool camera_distance(glaze::scene::scene_entity* entity, float& distance)
{
    if (!has_transform(objects::find_type(entity)) || !glaze::rendering::scene_render::frame_pipe::has_camera()) {
        return false;
    }

    distance = glm::distance(glaze::rendering::scene_render::frame_pipe::get_camera().position, entity->get_position());

    return true;
}

std::string format_distance(float distance)
{
    char text[32];

    if (distance >= KILOMETER) {
        std::snprintf(text, sizeof(text), "%.2f km", distance / KILOMETER);
    }

    else {
        std::snprintf(text, sizeof(text), "%.1f m", distance);
    }

    return text;
}

std::string format_vec3(const glm::vec3& value)
{
    char text[96];
    std::snprintf(text, sizeof(text), "%.2f, %.2f, %.2f", value.x, value.y, value.z);

    return text;
}

void select_from_browser(glaze::scene::scene_entity* entity)
{
    objects::selection::select(entity->get_id());

    m_details_open        = true;
    m_picked_from_browser = true;
}

void draw_entity_tooltip(glaze::scene::scene_entity* entity, const type_style& style, bool is_inactive, bool is_player)
{
    if (!ImGui::BeginTooltip()) {
        return;
    }

    draw_badge(style);
    ImGui::TextUnformatted(objects::get_entity_name(entity).c_str());

    ImGui::TextDisabled("%s", style.label);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 3.0f);
    ImGui::TextDisabled("ID %llu", static_cast<unsigned long long>(entity->get_id()));

    if (has_transform(objects::find_type(entity))) {
        ImGui::Separator();

        ImGui::Text("location  %s", format_vec3(entity->get_position()).c_str());
        ImGui::Text("rotation  %s", format_vec3(entity->get_rotation()).c_str());
        ImGui::Text("scale     %s", format_vec3(entity->get_scale()).c_str());

        float distance = 0.0f;

        if (camera_distance(entity, distance)) {
            ImGui::Text("distance  %s", format_distance(distance).c_str());
        }
    }

    if (is_player || is_inactive) {
        ImGui::Separator();
    }

    if (is_player) {
        ImGui::TextColored(ACTIVE_COLOR, "current player");
    }

    if (is_inactive) {
        ImGui::TextColored(INACTIVE_COLOR, "inactive: only the first object of this type is used");
    }

    ImGui::EndTooltip();
}

void draw_entity_menu(glaze::scene::scene_entity* entity, bool is_player)
{
    if (!ImGui::BeginPopupContextItem()) {
        return;
    }

    const std::string name = objects::get_entity_name(entity);

    ImGui::TextDisabled("%s", name.c_str());
    ImGui::Separator();

    if (ImGui::MenuItem("Select")) {
        select_from_browser(entity);
    }

    if (ImGui::MenuItem("Copy Name")) {
        ImGui::SetClipboardText(name.c_str());
    }

    if (ImGui::MenuItem("Copy Location", nullptr, false, has_transform(objects::find_type(entity)))) {
        ImGui::SetClipboardText(format_vec3(entity->get_position()).c_str());
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Delete", nullptr, false, !is_player)) {
        delete_entity(entity);
    }

    ImGui::EndPopup();
}

void draw_entity_row(glaze::scene::scene_entity* entity, const type_style& style, bool is_inactive)
{
    const bool is_selected = entity == objects::selection::get_selected();
    const bool is_player   = entity == glaze::scene::it::get_player_entity();

    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    ImGui::PushID(entity);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_FramePadding;

    if (is_selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    ImGui::TreeNodeEx("##entity", flags);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
        select_from_browser(entity);
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        draw_entity_tooltip(entity, style, is_inactive, is_player);
    }

    draw_entity_menu(entity, is_player);

    if (is_selected && m_scroll_to_selected) {
        ImGui::SetScrollHereY();

        m_scroll_to_selected = false;
    }

    ImGui::SameLine();
    draw_badge(style);

    if (is_inactive) {
        ImGui::PushStyleColor(ImGuiCol_Text, INACTIVE_COLOR);
    }

    ImGui::TextUnformatted(objects::get_entity_name(entity).c_str());

    if (is_inactive) {
        ImGui::PopStyleColor();
    }

    if (is_player) {
        ImGui::SameLine();
        ImGui::TextColored(ACTIVE_COLOR, "(Current Player)");
    }

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", style.label);

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();

    float distance = 0.0f;

    if (camera_distance(entity, distance)) {
        const std::string text = format_distance(distance);

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text.c_str()).x);
        ImGui::TextUnformatted(text.c_str());
    }

    else {
        ImGui::TextDisabled("-");
    }

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%llu", static_cast<unsigned long long>(entity->get_id()));

    ImGui::PopID();
}

void draw_toolbar(int total)
{
    if (ImGui::Button(ADD_LABEL)) {
        ImGui::OpenPopup(ADD_POPUP);
    }

    if (ImGui::BeginPopup(ADD_POPUP)) {
        ImGui::TextDisabled("spawn at the screen center");
        ImGui::Separator();

        const std::vector<const objects::object_type*> types = creatable_types();

        for (int i = 0; i < static_cast<int>(types.size()); i++) {
            const type_style style = style_of(types[i]);

            draw_badge(style);

            if (ImGui::MenuItem(style.label)) {
                objects::clear_last_error();

                m_create_type  = i;
                m_details_open = true;
            }
        }

        ImGui::EndPopup();
    }

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

    const bool has_filter = m_filter.IsActive();

    const float clear_width = has_filter ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x : 0.0f;

    char hint[64];
    std::snprintf(hint, sizeof(hint), "Search %d objects...", total);

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clear_width);

    if (ImGui::InputTextWithHint("##search", hint, m_filter.InputBuf, IM_ARRAYSIZE(m_filter.InputBuf))) {
        m_filter.Build();
    }

    if (has_filter) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

        if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) {
            m_filter.Clear();
        }
    }
}

void draw_browser()
{
    place_panel(0.0f, BROWSER_SHARE);

    if (!ImGui::Begin(BROWSER_TITLE, &m_browser_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    std::vector<glaze::scene::scene_entity*> entities;

    for (const std::unique_ptr<glaze::scene::scene_entity>& entity : glaze::scene::it::get_all_entities()) {
        if (!glaze::scene::it::util::is_entity_pending_erase(entity.get())) {
            entities.push_back(entity.get());
        }
    }

    draw_toolbar(static_cast<int>(entities.size()));

    std::vector<const objects::object_type*> groups;

    for (const objects::object_type& type : objects::get_types()) {
        groups.push_back(&type);
    }

    groups.push_back(nullptr);

    const float footer_height = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersOuter
        | ImGuiTableFlags_BordersInnerV
        | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_Hideable
        | ImGuiTableFlags_ScrollY;

    int shown = 0;

    if (ImGui::BeginTable("browser", 4, table_flags, ImVec2(0.0f, -footer_height))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(TYPE_COLUMN_SAMPLE).x);
        ImGui::TableSetupColumn("Distance", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(DISTANCE_COLUMN_SAMPLE).x);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, ImGui::CalcTextSize(ID_COLUMN_SAMPLE).x);
        ImGui::TableHeadersRow();

        for (const objects::object_type* group : groups) {
            std::vector<glaze::scene::scene_entity*> members;

            for (glaze::scene::scene_entity* entity : entities) {
                if (objects::find_type(entity) == group) {
                    members.push_back(entity);
                }
            }

            std::vector<size_t> visible;

            for (size_t i = 0; i < members.size(); i++) {
                if (m_filter.PassFilter(objects::get_entity_name(members[i]).c_str()) || m_filter.PassFilter(style_of(group).label)) {
                    visible.push_back(i);
                }
            }

            if (visible.empty()) {
                continue;
            }

            shown += static_cast<int>(visible.size());

            const type_style style = style_of(group);

            const bool has_selected = std::any_of(visible.begin(), visible.end(), [&](size_t i) { return members[i] == objects::selection::get_selected(); });

            if ((has_selected && m_scroll_to_selected) || m_filter.IsActive()) {
                ImGui::SetNextItemOpen(true);
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            const bool is_open = ImGui::TreeNodeEx(style.label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_FramePadding, "%s", "");

            ImGui::SameLine();
            draw_badge(style);
            ImGui::TextUnformatted(style.label);
            ImGui::SameLine();
            ImGui::TextDisabled("(%d)", static_cast<int>(visible.size()));

            if (!is_open) {
                continue;
            }

            for (size_t i : visible) {
                draw_entity_row(members[i], style, group != nullptr && group->only_first_active && i > 0);
            }

            ImGui::TreePop();
        }

        if (shown == 0) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled(m_filter.IsActive() ? "no objects match the search" : "the scene is empty");
        }

        ImGui::EndTable();
    }

    glaze::scene::scene_entity* selected = objects::selection::get_selected();

    if (m_filter.IsActive()) {
        ImGui::TextDisabled("%d of %d objects", shown, static_cast<int>(entities.size()));
    }

    else {
        ImGui::TextDisabled("%d objects", static_cast<int>(entities.size()));
    }

    if (selected != nullptr) {
        ImGui::SameLine();
        ImGui::TextDisabled("|  selected: %s", objects::get_entity_name(selected).c_str());
    }

    ImGui::End();
}

bool edit_axes(const char* label, glm::vec3& value, float drag_speed, const objects::selection::transform_axis_prefs& prefs, float reset_value)
{
    bool changed = false;

    ImGui::PushID(label);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    ImGui::TableNextColumn();

    const float spacing     = ImGui::GetStyle().ItemInnerSpacing.x;
    const float frame       = ImGui::GetFrameHeight();
    const float field_width = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f - frame;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));

    for (int axis = 0; axis < 3; axis++) {
        if (axis > 0) {
            ImGui::SameLine(0.0f, spacing);
        }

        ImGui::PushID(axis);

        ImGui::PushStyleColor(ImGuiCol_Button, AXIS_COLORS[axis]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AXIS_COLORS[axis]);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AXIS_COLORS[axis]);

        if (ImGui::Button(AXIS_LABELS[axis], ImVec2(frame, frame))) {
            value[axis] = reset_value;
            changed     = true;
        }

        ImGui::PopStyleColor(3);

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("reset %s to %g", AXIS_LABELS[axis], reset_value);
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(field_width);

        if (ImGui::DragFloat("##value", &value[axis], drag_speed, 0.0f, 0.0f, "%.3f")) {
            if (prefs.snap && prefs.step > 0.0f) {
                value[axis] = std::round(value[axis] / prefs.step) * prefs.step;
            }

            changed = true;
        }

        ImGui::PopID();
    }

    ImGui::PopStyleVar();
    ImGui::PopID();

    return changed;
}

bool begin_submenu(const char* name)
{
    return ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth);
}

void draw_transform(glaze::scene::scene_entity* entity)
{
    if (!ImGui::BeginTable("transform", 2, ImGuiTableFlags_SizingStretchProp)) {
        return;
    }

    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, TRANSFORM_LABEL_WIDTH);
    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, 1.0f - TRANSFORM_LABEL_WIDTH);

    glm::vec3 position = entity->get_position();
    glm::vec3 rotation = entity->get_rotation();
    glm::vec3 scale    = entity->get_scale();

    bool changed = false;

    changed |= edit_axes("Location", position, MOVE_DRAG_SPEED,   objects::selection::get_move_prefs(),   0.0f);
    changed |= edit_axes("Rotation", rotation, ROTATE_DRAG_SPEED, objects::selection::get_rotate_prefs(), 0.0f);
    changed |= edit_axes("Scale",    scale,    SCALE_DRAG_SPEED,  objects::selection::get_scale_prefs(),  1.0f);

    ImGui::EndTable();

    if (changed) {
        entity->set_transform(position, rotation, scale);
    }
}

void draw_header(const type_style& style, std::string title, const char* note, const char* caption, const char* id)
{
    const float frame   = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;

    draw_badge(style, frame);

    const float note_width = note != nullptr ? ImGui::CalcTextSize(note).x + spacing : 0.0f;

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - note_width);
    ImGui::InputText("##title", title.data(), title.size() + 1, ImGuiInputTextFlags_ReadOnly);

    if (note != nullptr) {
        ImGui::SameLine(0.0f, spacing);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ACTIVE_COLOR, "%s", note);
    }

    ImGui::TextDisabled("%s", caption);

    if (id != nullptr) {
        ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(id).x);
        ImGui::TextDisabled("%s", id);
    }

    ImGui::Separator();
}

void draw_entity_details(glaze::scene::scene_entity* entity)
{
    const objects::object_type* type = objects::find_type(entity);

    char id[32];
    std::snprintf(id, sizeof(id), "ID %llu", static_cast<unsigned long long>(entity->get_id()));

    draw_header(style_of(type), objects::get_entity_name(entity), entity == glaze::scene::it::get_player_entity() ? "(Current Player)" : nullptr, style_of(type).label, id);

    ImGui::Spacing();

    const bool show_transform = has_transform(type);
    const bool show_fields    = type != nullptr && type->draw_fields;

    if ((show_transform || show_fields) && ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (show_transform && begin_submenu("Transform")) {
            draw_transform(entity);
            ImGui::TreePop();
        }

        if (show_fields && begin_submenu("Custom Properties")) {
            type->draw_fields(entity);
            ImGui::TreePop();
        }
    }

    draw_error();

    if (!ImGui::CollapsingHeader("Management", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    const bool is_player = entity == glaze::scene::it::get_player_entity();

    ImGui::BeginDisabled(is_player);

    ImGui::PushStyleColor(ImGuiCol_Button, DELETE_COLOR);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, DELETE_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, DELETE_COLOR);

    if (ImGui::Button("Delete")) {
        delete_entity(entity);
    }

    ImGui::PopStyleColor(3);

    ImGui::EndDisabled();

    if (is_player) {
        ImGui::SameLine();
        ImGui::TextDisabled("the active camera cannot be deleted");
    }
}

void draw_create_details()
{
    const std::vector<const objects::object_type*> types = creatable_types();

    if (m_create_type >= static_cast<int>(types.size())) {
        m_create_type = NO_CREATE_TYPE;
        return;
    }

    const objects::object_type* type = types[m_create_type];

    draw_header(style_of(type), std::string("New ") + style_of(type).label, nullptr, "spawns at the screen center", nullptr);

    ImGui::Spacing();

    if (type->draw_draft && ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen) && begin_submenu("Custom Properties")) {
        type->draw_draft();
        ImGui::TreePop();
    }

    draw_error();

    if (!ImGui::CollapsingHeader("Management", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Button, CREATE_COLOR);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, CREATE_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, CREATE_COLOR);

    if (ImGui::Button("Create")) {
        objects::clear_last_error();

        objects::selection::request_spawn(type->create);
    }

    ImGui::PopStyleColor(3);

    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
        m_create_type = NO_CREATE_TYPE;
    }
}

void draw_details()
{
    place_panel(BROWSER_SHARE, 1.0f - BROWSER_SHARE);

    if (!ImGui::Begin(DETAILS_TITLE, &m_details_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    glaze::scene::scene_entity* entity = objects::selection::get_selected();

    if (m_create_type != NO_CREATE_TYPE) {
        draw_create_details();
    }

    else if (entity != nullptr) {
        draw_entity_details(entity);
    }

    else {
        ImGui::TextDisabled("select an object in the Objects Browser or the viewport");
    }

    ImGui::End();
}

const bool registered_browser = [] {
    panels::register_panel({ "Scene", BROWSER_TITLE, &m_browser_open, draw_browser });
    return true;
}();

const bool registered_details = [] {
    panels::register_panel({ "Scene", DETAILS_TITLE, &m_details_open, draw_details });
    return true;
}();

}

void update()
{
    track_selection();
}

}

#endif
