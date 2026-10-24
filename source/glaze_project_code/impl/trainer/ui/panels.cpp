#include "glp/trainer/ui/panels.h"

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include <imgui.h>

#include <algorithm>
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
    std::vector<const panel_registration*> matched;

    for (const panel_registration& entry : registry()) {
        if (!entry.hidden_in_menu && std::strcmp(entry.menu, menu) == 0) {
            matched.push_back(&entry);
        }
    }

    std::stable_sort(matched.begin(), matched.end(), [](const panel_registration* left, const panel_registration* right) {
        return left->order < right->order;
    });

    for (const panel_registration* entry : matched) {
        ImGui::MenuItem(entry->label, nullptr, entry->open);
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
