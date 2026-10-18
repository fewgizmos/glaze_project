#pragma once

#include "glp/glaze/rendering/vk/resources.h"

#include <vulkan/vulkan_core.h>

namespace glaze::rendering::scene_render::atmosphere
{

void init_all();
void clean_all();

void handle_all();
void dispatch();

VkDeviceAddress get_params_address();

bool is_active();

float get_fog_density();

const image& get_transmittance_image();
const image& get_multi_scattering_image();
const image& get_sky_view_image();

}
