#include "glp/trainer/ui/scene_management.h"

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include "glp/trainer/objects/object_types.h"
#include "glp/trainer/ui/panels.h"

#include "glp/glaze/scene/scene.h"

#include <imgui.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace trainer::ui::scene_management
{

namespace
{

constexpr const char* MANAGEMENT_TITLE = "Scene Management";
constexpr const char* PROPERTIES_TAB   = "Properties";
constexpr const char* NO_PLAYER_LABEL  = "<none>";

constexpr float WINDOW_WIDTH  = 420.0f;
constexpr float WINDOW_HEIGHT = 180.0f;

bool m_open = false;

std::vector<glaze::scene::scene_entity*> collect_live_entities()
{
    std::vector<glaze::scene::scene_entity*> result;

    for (const std::unique_ptr<glaze::scene::scene_entity>& ent : glaze::scene::it::get_all_entities()) {
        if (glaze::scene::it::util::is_entity_pending_erase(ent.get())) {
            continue;
        }

        result.push_back(ent.get());
    }

    return result;
}

std::string label_for(glaze::scene::scene_entity* entity)
{
    if (entity == nullptr) {
        return NO_PLAYER_LABEL;
    }

    return trainer::objects::get_entity_name(entity);
}

void draw_player_combo()
{
    const std::vector<glaze::scene::scene_entity*> entities = collect_live_entities();

    glaze::scene::scene_entity* current = glaze::scene::it::get_player_entity();

    const std::string preview = label_for(current);

    ImGui::TextUnformatted("Player Entity");

    ImGui::SetNextItemWidth(-FLT_MIN);

    if (!ImGui::BeginCombo("##player_entity", preview.c_str())) {
        return;
    }

    const bool none_selected = current == nullptr;

    if (ImGui::Selectable(NO_PLAYER_LABEL, none_selected)) {
        glaze::scene::it::set_player_entity(nullptr);
    }

    if (none_selected) {
        ImGui::SetItemDefaultFocus();
    }

    for (glaze::scene::scene_entity* entity : entities) {
        const bool selected = entity == current;

        const std::string name = label_for(entity);

        ImGui::PushID(static_cast<int>(static_cast<uint64_t>(entity->get_id()) & 0x7FFFFFFFull));

        if (ImGui::Selectable(name.c_str(), selected)) {
            glaze::scene::it::set_player_entity(entity);
        }

        if (selected) {
            ImGui::SetItemDefaultFocus();
        }

        ImGui::PopID();
    }

    ImGui::EndCombo();
}

void draw_properties_tab()
{
    draw_player_combo();
}

void draw_window()
{
    ImGui::SetNextWindowSize(ImVec2(WINDOW_WIDTH, WINDOW_HEIGHT), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(MANAGEMENT_TITLE, &m_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("##scene_management_tabs")) {
        if (ImGui::BeginTabItem(PROPERTIES_TAB)) {
            draw_properties_tab();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

const bool registered = [] {
    panels::register_panel({ "Scene", MANAGEMENT_TITLE, &m_open, draw_window, 3 });
    return true;
}();

}

}

#endif
