#include "glp/trainer/ui/ui.h"

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include "glp/trainer/ui/file_field.h"
#include "glp/trainer/ui/metrics.h"
#include "glp/trainer/ui/panels.h"
#include "glp/trainer/ui/scene_objects.h"

#include "glp/trainer/objects/selection.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include <imgui.h>

namespace trainer::ui::it
{

namespace
{

constexpr const char* TOOLKIT_LABEL = "INTERNAL DEVELOPER TOOLKIT";

void draw_toolkit_label()
{
    const float label_width = ImGui::CalcTextSize(TOOLKIT_LABEL).x;

    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - label_width - ImGui::GetStyle().WindowPadding.x - 1.0f);
    ImGui::TextDisabled("%s", TOOLKIT_LABEL);

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImU32  col = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + 1.0f, min.y), col, TOOLKIT_LABEL);
}

}

void cleanup()
{
    file_field::cleanup();
    metrics::cleanup();
}

void draw()
{
    metrics::update();

    objects::selection::update();

    file_field::update();

    scene_objects::update();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Scene")) {
            panels::draw_menu_items("Scene");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("World")) {
            panels::draw_menu_items("World");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Metrics")) {
            panels::draw_menu_items("Metrics");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Runtime Resources")) {
            if (ImGui::MenuItem("Reload Shaders")) {
                glaze::rendering::scene_render::res::shaders::reload_all();
            }

            ImGui::EndMenu();
        }

        draw_toolkit_label();

        ImGui::EndMainMenuBar();
    }

    panels::draw_panels();

    objects::selection::draw();
}

}

#endif
