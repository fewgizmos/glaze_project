#pragma once

#include "glp/glaze/rendering/vk/resources.h"

#include <cstdint>
#include <string>
#include <vector>

namespace glaze::rendering::scene_render
{

struct texture
{
    image target;
    uint32_t index = 0;
};

}

namespace glaze::rendering::scene_render::res::textures
{

const texture* build_raw(const void* pixels, uint32_t width, uint32_t height, bool is_srgb, uint32_t mip_levels);
const texture* build_from_file(const std::string& path, bool is_srgb, uint32_t mip_levels);

void erase(const texture* target);
void erase_all();

void destroy_erased();

std::vector<const texture*> take_built();

}
