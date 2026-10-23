#pragma once

#include "glp/glaze/rendering/vk/resources.h"

#include <cstdint>

namespace glaze::rendering::scene_render::outline_mask
{

void init_all();
void clean_all();

void handle_all();
void dispatch();

void set_entity(uint32_t entity_id);

const image& get_mask_image();

}
