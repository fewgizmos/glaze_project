#pragma once

#include "glp/glaze/utilities/core/assert.h"

#include <fstream>
#include <string>

namespace glaze::utilities::file
{

inline std::string read_raw(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    GLAZE_ASSERT(!file.is_open(), "failed to open file: " + path);

    std::string content(static_cast<size_t>(file.tellg()), '\0');

    file.seekg(0);
    file.read(content.data(), static_cast<std::streamsize>(content.size()));

    return content;
}

}
