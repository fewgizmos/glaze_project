#pragma once

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include <imgui.h>

namespace trainer::ui::metrics
{

void update();
void cleanup();

bool get_minimized_bounds(ImVec2& position, ImVec2& size);

}

#endif
