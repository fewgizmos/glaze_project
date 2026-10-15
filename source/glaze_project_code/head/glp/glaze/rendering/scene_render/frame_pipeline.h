#pragma once

#include "glp/glaze/rendering/scene_render/resources/gpu_camera.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_model.h"

#include <vulkan/vulkan_core.h>

namespace glaze::rendering::scene_render::frame_pipe
{

void init_all();
void clean_all();

void clear();

void set_camera(const gpu_camera& camera);
void submit_model(const gpu_model& model);

bool has_camera();
const gpu_camera& get_camera();

void handle_all();
void draw_all();

VkDeviceAddress get_objects_address();
VkDeviceAddress get_materials_address();

}
