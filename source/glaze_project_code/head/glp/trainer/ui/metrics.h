#pragma once

#ifndef NDEBUG

#include <imgui.h>

namespace trainer::ui::metrics
{

void update();
void cleanup();

void draw_menu();
void draw();

bool get_minimized_bounds(ImVec2& position, ImVec2& size);

}

#endif
