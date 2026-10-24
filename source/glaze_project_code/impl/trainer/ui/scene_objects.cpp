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
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace trainer::ui::scene_objects
{

namespace
{

namespace objects = trainer::objects;

constexpr uint64_t NO_ENTITY      = 0;
constexpr int      NO_CREATE_TYPE = -1;

constexpr float MOVE_DRAG_SPEED   = 0.05f;
constexpr float ROTATE_DRAG_SPEED = 0.5f;
constexpr float SCALE_DRAG_SPEED  = 0.01f;

constexpr float KILOMETER = 1000.0f;

constexpr ImU32 HUE_STEPS      = 360;
constexpr float HUE_SATURATION = 0.55f;
constexpr float HUE_VALUE      = 0.92f;

constexpr float BROWSER_NAME_WIDTH     = 0.42f;
constexpr float BROWSER_TYPE_WIDTH     = 0.26f;
constexpr float BROWSER_DISTANCE_WIDTH = 0.20f;
constexpr float BROWSER_ID_WIDTH       = 0.12f;

constexpr float SUMMARY_COLUMN_WIDTH = 0.25f;

constexpr float FIELDS_LABEL_WIDTH    = 0.4f;
constexpr float TRANSFORM_LABEL_WIDTH = FIELDS_LABEL_WIDTH;

constexpr float PANEL_WIDTH_SHARE    = 0.25f;
constexpr float BROWSER_HEIGHT_SHARE = 0.35f;
constexpr float DETAILS_HEIGHT_SHARE = 0.65f;
constexpr float EDGE_MARGIN          = 8.0f;
constexpr float WINDOW_GAP           = 3.0f;

constexpr float CREATE_BG_ALPHA = 0.85f;

const ImVec2 CREATE_WINDOW_SIZE = ImVec2(460.0f, 480.0f);

const ImVec4 ERROR_COLOR    = ImVec4(0.95f, 0.40f, 0.35f, 1.0f);
const ImVec4 INACTIVE_COLOR = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
const ImVec4 ACTIVE_COLOR   = ImVec4(0.45f, 0.80f, 0.40f, 1.0f);

const ImVec4 CREATE_COLOR         = ImVec4(0.20f, 0.45f, 0.22f, 1.0f);
const ImVec4 CREATE_HOVERED_COLOR = ImVec4(0.26f, 0.58f, 0.28f, 1.0f);
const ImVec4 DELETE_COLOR         = ImVec4(0.55f, 0.18f, 0.16f, 1.0f);
const ImVec4 DELETE_HOVERED_COLOR = ImVec4(0.72f, 0.24f, 0.20f, 1.0f);
const ImVec4 PLAYER_COLOR         = ImVec4(0.22f, 0.47f, 0.78f, 1.0f);
const ImVec4 PLAYER_HOVERED_COLOR = ImVec4(0.28f, 0.60f, 0.92f, 1.0f);

struct type_style
{
    const char* label;
    ImVec4      color;
};

const std::array<ImVec4, 3> AXIS_COLORS = {
    ImVec4(0.80f, 0.22f, 0.20f, 1.0f),
    ImVec4(0.32f, 0.62f, 0.18f, 1.0f),
    ImVec4(0.20f, 0.40f, 0.85f, 1.0f)
};

const std::array<const char*, 3> AXIS_LABELS = { "X", "Y", "Z" };

bool m_browser_open = false;
bool m_details_open = false;
bool m_create_open  = false;

ImGuiTextFilter m_filter;

int m_create_type = NO_CREATE_TYPE;

uint64_t m_last_selected = NO_ENTITY;

bool m_scroll_to_selected  = false;
bool m_picked_from_browser = false;

void draw_bold_bullet(const char* text)
{
    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::SetWindowFontScale(1.08f);

    ImGui::Bullet();

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImU32  col = ImGui::GetColorU32(ImGuiCol_Text);

    ImGui::TextUnformatted(text);
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x + 1.0f, pos.y), col, text);

    ImGui::SetWindowFontScale(1.0f);

    ImGui::Spacing();
    ImGui::Spacing();
}

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

type_style style_of(const objects::object_type* type)
{
    const char* label = type != nullptr && type->label[0] != '\0' ? type->label : "Entity";

    const ImU32 hue_hash = ImHashStr(type != nullptr ? type->name : "Entity");

    ImVec4 color(0.0f, 0.0f, 0.0f, 1.0f);

    ImGui::ColorConvertHSVtoRGB(static_cast<float>(hue_hash % HUE_STEPS) / static_cast<float>(HUE_STEPS), HUE_SATURATION, HUE_VALUE, color.x, color.y, color.z);

    return { label, color };
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

    if (selected != nullptr) {
        m_create_type = NO_CREATE_TYPE;
        m_create_open = false;
    }

    objects::clear_last_error();
}

void delete_entity(glaze::scene::scene_entity* entity)
{
    if (entity == objects::selection::get_selected()) {
        objects::selection::clear();
    }

    glaze::scene::it::erase_entity(entity);
}

void select_from_browser(glaze::scene::scene_entity* entity)
{
    objects::selection::select(entity->get_id());

    m_details_open        = true;
    m_picked_from_browser = true;
}

void draw_error()
{
    if (objects::get_last_error().empty()) {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ERROR_COLOR);
    ImGui::TextWrapped("%s", objects::get_last_error().c_str());
    ImGui::PopStyleColor();
}

void draw_entity_tooltip(glaze::scene::scene_entity* entity, const type_style& style, bool is_inactive, bool is_player)
{
    if (!ImGui::BeginTooltip()) {
        return;
    }

    ImGui::TextUnformatted(objects::get_entity_name(entity).c_str());

    ImGui::TextColored(style.color, "%s", style.label);
    ImGui::SameLine();
    ImGui::TextDisabled("ID %llu", static_cast<unsigned long long>(entity->get_id()));

    if (has_transform(objects::find_type(entity))) {
        ImGui::Separator();

        ImGui::Text("position  %s", format_vec3(entity->get_position()).c_str());
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

void draw_entity_menu(glaze::scene::scene_entity* entity)
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

    if (ImGui::MenuItem("Copy Position", nullptr, false, has_transform(objects::find_type(entity)))) {
        ImGui::SetClipboardText(format_vec3(entity->get_position()).c_str());
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Delete")) {
        delete_entity(entity);
    }

    ImGui::EndPopup();
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

        ImGui::PushStyleColor(ImGuiCol_Button,        AXIS_COLORS[axis]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AXIS_COLORS[axis]);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  AXIS_COLORS[axis]);

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
            changed = true;
        }

        if (prefs.snap && prefs.step > 0.0f && ImGui::IsItemDeactivatedAfterEdit()) {
            value[axis] = std::round(value[axis] / prefs.step) * prefs.step;
            changed     = true;
        }

        ImGui::PopID();
    }

    ImGui::PopStyleVar();
    ImGui::PopID();

    return changed;
}

void draw_transform(glaze::scene::scene_entity* entity)
{
    if (!ImGui::BeginTable("transform", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        return;
    }

    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, TRANSFORM_LABEL_WIDTH);
    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, 1.0f - TRANSFORM_LABEL_WIDTH);

    glm::vec3 position = entity->get_position();
    glm::vec3 rotation = entity->get_rotation();
    glm::vec3 scale    = entity->get_scale();

    bool changed = false;

    changed |= edit_axes("Position", position, MOVE_DRAG_SPEED,   objects::selection::get_move_prefs(),   0.0f);
    changed |= edit_axes("Rotation", rotation, ROTATE_DRAG_SPEED, objects::selection::get_rotate_prefs(), 0.0f);
    changed |= edit_axes("Scale",    scale,    SCALE_DRAG_SPEED,  objects::selection::get_scale_prefs(),  1.0f);

    ImGui::EndTable();

    if (changed) {
        entity->set_transform(position, rotation, scale);
    }
}

void draw_fields_table(const std::function<void()>& draw)
{
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;

    if (!ImGui::BeginTable("fields", 2, flags)) {
        return;
    }

    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, FIELDS_LABEL_WIDTH);
    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, 1.0f - FIELDS_LABEL_WIDTH);

    draw();

    ImGui::EndTable();
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

    draw_entity_menu(entity);

    if (is_selected && m_scroll_to_selected) {
        ImGui::SetScrollHereY();

        m_scroll_to_selected = false;
    }

    ImGui::SameLine();

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
    ImGui::TextColored(style.color, "%s", style.label);

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

void draw_browser_toolbar(int total)
{
    if (ImGui::Button("+ Create")) {
        ImGui::OpenPopup("add_object");
    }

    if (ImGui::BeginPopup("add_object")) {
        ImGui::TextDisabled("Typed");
        ImGui::Separator();

        const std::vector<const objects::object_type*> types = creatable_types();

        for (int i = 0; i < static_cast<int>(types.size()); i++) {
            const type_style style = style_of(types[i]);

            if (ImGui::MenuItem(style.label)) {
                objects::clear_last_error();

                m_create_type = i;
                m_create_open = true;
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

void draw_browser_summary(int total, int shown)
{
    const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame;

    if (!ImGui::BeginTable("browser_summary", 3, flags)) {
        return;
    }

    for (int i = 0; i < 3; i++) {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, 1.0f / 3.0f);
    }

    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    ImGui::TextDisabled("objects");
    ImGui::Text("%d", total);

    ImGui::TableNextColumn();
    ImGui::TextDisabled("filter");

    if (m_filter.IsActive()) {
        ImGui::Text("%d of %d", shown, total);
    }

    else {
        ImGui::TextDisabled("off");
    }

    ImGui::TableNextColumn();
    ImGui::TextDisabled("selected");

    glaze::scene::scene_entity* selected = objects::selection::get_selected();

    if (selected != nullptr) {
        ImGui::TextUnformatted(objects::get_entity_name(selected).c_str());
    }

    else {
        ImGui::TextDisabled("none");
    }

    ImGui::EndTable();
}

int draw_browser_table(const std::vector<glaze::scene::scene_entity*>& entities)
{
    std::vector<const objects::object_type*> groups;

    for (const objects::object_type& type : objects::get_types()) {
        groups.push_back(&type);
    }

    groups.push_back(nullptr);

    const ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersOuter
        | ImGuiTableFlags_BordersInnerV
        | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_Hideable
        | ImGuiTableFlags_ScrollY;

    int shown = 0;

    if (!ImGui::BeginTable("entities", 4, flags, ImVec2(0.0f, 0.0f))) {
        return shown;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name",     ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, BROWSER_NAME_WIDTH);
    ImGui::TableSetupColumn("Type",     ImGuiTableColumnFlags_WidthStretch, BROWSER_TYPE_WIDTH);
    ImGui::TableSetupColumn("Distance", ImGuiTableColumnFlags_WidthStretch, BROWSER_DISTANCE_WIDTH);
    ImGui::TableSetupColumn("ID",       ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultHide, BROWSER_ID_WIDTH);
    ImGui::TableHeadersRow();

    for (const objects::object_type* group : groups) {
        std::vector<glaze::scene::scene_entity*> members;

        for (glaze::scene::scene_entity* entity : entities) {
            if (objects::find_type(entity) == group) {
                members.push_back(entity);
            }
        }

        const type_style style = style_of(group);

        std::vector<size_t> visible;

        for (size_t i = 0; i < members.size(); i++) {
            if (m_filter.PassFilter(objects::get_entity_name(members[i]).c_str()) || m_filter.PassFilter(style.label)) {
                visible.push_back(i);
            }
        }

        if (visible.empty()) {
            continue;
        }

        shown += static_cast<int>(visible.size());

        const bool has_selected = std::any_of(visible.begin(), visible.end(), [&](size_t i) { return members[i] == objects::selection::get_selected(); });

        if ((has_selected && m_scroll_to_selected) || m_filter.IsActive()) {
            ImGui::SetNextItemOpen(true);
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        const bool is_open = ImGui::TreeNodeEx(style.label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_FramePadding, "%s", "");

        ImGui::SameLine();
        ImGui::TextColored(style.color, "%s", style.label);
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

    return shown;
}

void draw_browser()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    const float available_h = vp->WorkSize.y - EDGE_MARGIN * 2.0f - WINDOW_GAP;

    const float w = vp->WorkSize.x * PANEL_WIDTH_SHARE;
    const float h = available_h * BROWSER_HEIGHT_SHARE;
    const float x = vp->WorkPos.x + EDGE_MARGIN;
    const float y = vp->WorkPos.y + EDGE_MARGIN;

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(1.0f);

    if (!ImGui::Begin("Objects Browser", &m_browser_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    std::vector<glaze::scene::scene_entity*> entities;

    for (const std::unique_ptr<glaze::scene::scene_entity>& entity : glaze::scene::it::get_all_entities()) {
        if (!glaze::scene::it::util::is_entity_pending_erase(entity.get())) {
            entities.push_back(entity.get());
        }
    }

    draw_browser_toolbar(static_cast<int>(entities.size()));

    ImGui::Spacing();

    int shown = 0;

    for (glaze::scene::scene_entity* entity : entities) {
        const type_style style = style_of(objects::find_type(entity));

        if (m_filter.PassFilter(objects::get_entity_name(entity).c_str()) || m_filter.PassFilter(style.label)) {
            shown++;
        }
    }

    draw_browser_summary(static_cast<int>(entities.size()), shown);

    ImGui::Spacing();

    draw_browser_table(entities);

    ImGui::End();
}

void draw_management_tab(glaze::scene::scene_entity* entity, bool is_player)
{
    ImGui::Spacing();

    ImGui::BeginDisabled(is_player);

    ImGui::PushStyleColor(ImGuiCol_Button,        PLAYER_COLOR);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, PLAYER_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  PLAYER_COLOR);

    if (ImGui::Button("Set Current Player")) {
        glaze::scene::it::set_player_entity(entity);
    }

    ImGui::PopStyleColor(3);

    ImGui::EndDisabled();

    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button,        DELETE_COLOR);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, DELETE_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  DELETE_COLOR);

    if (ImGui::Button("Delete")) {
        delete_entity(entity);
    }

    ImGui::PopStyleColor(3);
}

void draw_entity_details(glaze::scene::scene_entity* entity)
{
    const objects::object_type* type  = objects::find_type(entity);
    const type_style            style = style_of(type);

    const bool is_player = entity == glaze::scene::it::get_player_entity();

    const ImGuiTableFlags summary_flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame;

    if (ImGui::BeginTable("details_summary", 4, summary_flags)) {
        for (int i = 0; i < 4; i++) {
            ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, SUMMARY_COLUMN_WIDTH);
        }

        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::TextDisabled("Type");
        ImGui::TextColored(style.color, "%s", style.label);

        ImGui::TableNextColumn();
        ImGui::TextDisabled("ID");
        ImGui::Text("%llu", static_cast<unsigned long long>(entity->get_id()));

        ImGui::TableNextColumn();
        ImGui::TextDisabled("Distance");

        float distance = 0.0f;

        if (camera_distance(entity, distance)) {
            ImGui::Text("%s", format_distance(distance).c_str());
        }

        else {
            ImGui::TextDisabled("-");
        }

        ImGui::TableNextColumn();
        ImGui::TextDisabled("Current Player");

        if (is_player) {
            ImGui::TextColored(ACTIVE_COLOR, "True");
        }

        else {
            ImGui::TextColored(ERROR_COLOR, "False");
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();

    if (ImGui::BeginTabBar("details_tabs")) {
        const bool show_transform = has_transform(type);
        const bool show_fields    = type != nullptr && type->draw_fields;

        if ((show_transform || show_fields) && ImGui::BeginTabItem("Properties")) {
            ImGui::Spacing();

            if (ImGui::BeginTable("properties_header", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, FIELDS_LABEL_WIDTH);
                ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, 1.0f - FIELDS_LABEL_WIDTH);

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::TextDisabled("Key:");

                ImGui::TableNextColumn();
                ImGui::TextDisabled("Value:");

                ImGui::EndTable();
            }

            if (show_transform) {
                draw_bold_bullet("Entity:");
                draw_transform(entity);
            }

            if (show_fields) {
                draw_bold_bullet("Custom:");

                draw_fields_table([&] {
                    type->draw_fields(entity);
                });
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Management")) {
            draw_management_tab(entity, is_player);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    draw_error();
}

void draw_details()
{
    glaze::scene::scene_entity* entity = objects::selection::get_selected();

    const ImGuiViewport* vp = ImGui::GetMainViewport();

    const float available_h = vp->WorkSize.y - EDGE_MARGIN * 2.0f - WINDOW_GAP;

    const float w = vp->WorkSize.x * PANEL_WIDTH_SHARE;
    const float h = available_h * DETAILS_HEIGHT_SHARE;
    const float x = vp->WorkPos.x + EDGE_MARGIN;
    const float y = vp->WorkPos.y + vp->WorkSize.y - h - EDGE_MARGIN;

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(1.0f);

    char title[256];

    if (entity != nullptr) {
        std::snprintf(title, sizeof(title), "Object Details: %s###object_details", objects::get_entity_name(entity).c_str());
    }

    else {
        std::snprintf(title, sizeof(title), "Object Details: none selected###object_details");
    }

    if (!ImGui::Begin(title, &m_details_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (entity != nullptr) {
        draw_entity_details(entity);
    }

    else {
        ImGui::TextDisabled("select an object in the Objects Browser or the viewport");
    }

    ImGui::End();
}

void draw_create_body(const objects::object_type* type)
{
    if (type->draw_draft) {
        draw_bold_bullet("Properties:");

        draw_fields_table([&] {
            type->draw_draft();
        });
    }

    draw_bold_bullet("Actions:");

    ImGui::PushStyleColor(ImGuiCol_Button,        CREATE_COLOR);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, CREATE_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  CREATE_COLOR);

    if (ImGui::Button("Create")) {
        objects::clear_last_error();

        objects::selection::request_spawn(type->create);
    }

    ImGui::PopStyleColor(3);

    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
        m_create_type = NO_CREATE_TYPE;
        m_create_open = false;
    }

    draw_error();
}

void draw_create()
{
    ImGui::SetNextWindowSize(CREATE_WINDOW_SIZE, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(CREATE_BG_ALPHA);

    const std::vector<const objects::object_type*> types = creatable_types();

    if (m_create_type >= static_cast<int>(types.size())) {
        m_create_type = NO_CREATE_TYPE;
    }

    const objects::object_type* type  = m_create_type != NO_CREATE_TYPE ? types[m_create_type] : nullptr;
    const type_style            style = style_of(type);

    char title[128];

    if (type != nullptr) {
        std::snprintf(title, sizeof(title), "Create Object: %s###create_object", style.label);
    }

    else {
        std::snprintf(title, sizeof(title), "Create Object###create_object");
    }

    ImGui::PushStyleColor(ImGuiCol_Text, style.color);
    const bool opened = ImGui::Begin(title, &m_create_open, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleColor();

    if (!opened) {
        ImGui::End();
        return;
    }

    if (type != nullptr) {
        draw_create_body(type);
    }

    else {
        ImGui::TextDisabled("open + Create in Objects Browser to pick a type");
    }

    ImGui::End();
}

const bool registered_browser = [] {
    panels::register_panel({ "Scene", "Objects Browser", &m_browser_open, draw_browser });
    return true;
}();

const bool registered_details = [] {
    panels::register_panel({ "Scene", "Object Details", &m_details_open, draw_details });
    return true;
}();

const bool registered_create = [] {
    panels::register_panel({ "Scene", "Create Object", &m_create_open, draw_create, 0, true });
    return true;
}();

}

void update()
{
    track_selection();
}

}

#endif
