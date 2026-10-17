#pragma once

#include "glp/glaze/rendering/scene_render/resources/gpu_atmosphere.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_camera.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_environment.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_light.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_model.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_post.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace glaze::rendering::scene_render::frame_pipe
{

void init_all();
void clean_all();

void clear();

void set_camera(const gpu_camera& camera);
void set_post(const gpu_post& post);
void set_environment(const gpu_environment& environment);
void set_atmosphere(const gpu_atmosphere& atmosphere);
void submit_model(const gpu_model& model);
void submit_light(const gpu_light& light);

bool has_camera();
const gpu_camera& get_camera();
const gpu_post& get_post();

bool has_environment();
const gpu_environment& get_environment();

bool has_atmosphere();
const gpu_atmosphere& get_atmosphere();

void handle_all();
void draw_prepass();
void draw_all();

VkDeviceAddress get_objects_address();
VkDeviceAddress get_materials_address();
VkDeviceAddress get_lights_address();

uint32_t get_lights_count();

}
