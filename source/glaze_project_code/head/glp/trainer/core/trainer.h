#pragma once

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

namespace trainer::core::it
{

void startup();
void cleanup();

void frame_update();

}

#endif
