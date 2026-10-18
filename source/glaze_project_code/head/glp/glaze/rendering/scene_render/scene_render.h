#pragma once

#include "glp/glaze/rendering/vk/resources.h"

namespace glaze::rendering::scene_render::it
{

void init();
void cleanup();

void handle_all();

void draw_prepass();
void draw_all();

const image& get_depth_image();
const image& get_hdr_image();
const image& get_normal_image();
const image& get_velocity_image();
const image& get_gtao_image();
const image& get_taa_output_image();
const image& get_taa_history_image();

VkDeviceAddress get_frame_data_address();

VkDescriptorSetLayout get_scene_set_layout();
VkDescriptorSet get_scene_set();

float get_quality();

}
