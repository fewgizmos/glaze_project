#pragma once

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include <string>

namespace trainer::ui::property
{

std::string display_name(const char* label);

std::string begin_row(const char* label);

bool begin_category(const char* name);

}

#endif
