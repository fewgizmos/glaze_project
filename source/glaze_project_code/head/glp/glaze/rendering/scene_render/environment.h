#pragma once

#include "glp/glaze/rendering/vk/resources.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace glaze::rendering::scene_render::environment
{

constexpr uint32_t SKY_BINDING                     = 0;
constexpr uint32_t PREFILTERED_BINDING             = 1;
constexpr uint32_t BRDF_LUT_BINDING                = 2;
constexpr uint32_t AERIAL_PERSPECTIVE_BINDING      = 3;
constexpr uint32_t MOON_AERIAL_PERSPECTIVE_BINDING = 4;

constexpr uint32_t SET_SAMPLERS_COUNT = 5;

constexpr uint32_t PREFILTERED_SIZE = 128;
constexpr uint32_t PREFILTERED_MIPS = 5;

constexpr uint32_t BAKE_GROUP = 8;

constexpr uint32_t CUBE_FACES = 6;

constexpr VkFormat IBL_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;

void init_all();
void clean_all();

VkDescriptorSetLayout get_set_layout();
VkDescriptorSet get_default_set();

VkSampler get_cube_sampler();

void write_brdf_lut(VkDescriptorSet set);
void write_default_aerial_perspective(VkDescriptorSet set);

VkDescriptorSetLayout get_bake_set_layout();

void write_bake_set(VkDescriptorSet set, const image& source, VkImageView target);

uint32_t get_bake_groups(uint32_t size);

void transition_bake_target(VkCommandBuffer cmd, const image& target, VkImageLayout old_layout, VkImageLayout new_layout);

void record_prefilter(VkCommandBuffer cmd, const image& prefiltered, const VkDescriptorSet* mip_sets, float source_resolution);
void record_irradiance_sh(VkCommandBuffer cmd, VkDescriptorSet source_set, VkDeviceAddress output);

}
