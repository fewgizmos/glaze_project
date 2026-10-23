#ifndef NDEBUG

#include "glp/trainer/ui/panels.h"

#include <imgui.h>

#include <cstring>
#include <utility>
#include <vector>

namespace trainer::ui::panels
{

namespace
{

std::vector<panel_registration>& registry()
{
    static std::vector<panel_registration> instance;

    return instance;
}

}

void register_panel(panel_registration entry)
{
    registry().push_back(std::move(entry));
}

void draw_menu_items(const char* menu)
{
    for (const panel_registration& entry : registry()) {
        if (std::strcmp(entry.menu, menu) == 0) {
            ImGui::MenuItem(entry.label, nullptr, entry.open);
        }
    }
}

void draw_panels()
{
    for (const panel_registration& entry : registry()) {
        if (entry.open != nullptr && *entry.open) {
            entry.draw();
        }
    }
}

}

#endif
