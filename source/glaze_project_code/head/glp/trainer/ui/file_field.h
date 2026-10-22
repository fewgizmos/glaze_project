#pragma once

#ifndef NDEBUG

#include <string>

namespace trainer::ui::file_field
{

enum class pick_mode
{
    file,
    folder
};

bool draw(const char* label, std::string& path, const char* filters, pick_mode mode);

void draw_image_preview(const char* id, const std::string& image_path);

void update();
void cleanup();

}

#endif
