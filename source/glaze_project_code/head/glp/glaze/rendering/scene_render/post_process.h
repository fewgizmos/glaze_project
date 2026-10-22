#pragma once

#include <cstdint>

namespace glaze::rendering::scene_render::post_process
{

void init_all();
void clean_all();

void handle_all();
void draw_all();

void set_selected_entity(uint64_t entity_id);

}
