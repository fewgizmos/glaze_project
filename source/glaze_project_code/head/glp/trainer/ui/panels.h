#pragma once

#ifndef NDEBUG

#include <functional>

namespace trainer::ui::panels
{

struct panel_registration
{
    const char* menu;
    const char* label;
    bool* open;
    std::function<void()> draw;
};

void register_panel(panel_registration entry);

void draw_menu_items(const char* menu);
void draw_panels();

}

#endif
