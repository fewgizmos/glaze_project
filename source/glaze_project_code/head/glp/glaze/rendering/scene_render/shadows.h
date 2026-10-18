#pragma once

#include "glp/glaze/rendering/vk/resources.h"

#include <cstdint>

namespace glaze::rendering::scene_render::shadows
{

enum class debug_mode : uint32_t
{
    none   = 0,
    levels = 1,
    pages  = 2
};

struct shadow_stats
{
    uint32_t pool_pages      = 0;
    uint32_t free_pages      = 0;
    uint32_t requested_pages = 0;
    uint32_t rendered_pages  = 0;
    uint32_t failed_pages    = 0;
    uint32_t opaque_draws    = 0;
    uint32_t masked_draws    = 0;
    uint32_t casters         = 0;
    uint32_t invalidations   = 0;
};

void init_all();
void clean_all();

void handle_targets();
void handle_all();
void dispatch();

bool is_active();

VkDeviceAddress get_params_address();

const image& get_pool_image();
const image& get_contact_image();

const shadow_stats& get_stats();

void set_debug_mode(debug_mode mode);
debug_mode get_debug_mode();

void request_reset();

}
