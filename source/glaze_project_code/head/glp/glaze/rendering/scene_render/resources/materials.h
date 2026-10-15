#pragma once

#include "glp/glaze/rendering/scene_render/resources/shaders.h"
#include "glp/glaze/rendering/scene_render/resources/textures.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <vector>

namespace glaze::rendering::scene_render
{

constexpr uint32_t NO_TEXTURE = 0xFFFFFFFF;

enum class material_alpha_mode : uint32_t
{
    opaque = 0,
    mask   = 1
};

struct material_texture
{
    const texture* target = nullptr;
    uint32_t sampler_index = 0;
};

struct material_create_info
{
    const shader* target_shader = nullptr;

    glm::vec4 base_color = glm::vec4(1.0f);
    glm::vec3 emissive   = glm::vec3(0.0f);

    float metallic  = 1.0f;
    float roughness = 1.0f;

    float normal_scale       = 1.0f;
    float occlusion_strength = 1.0f;
    float alpha_cutoff       = 0.5f;

    material_alpha_mode alpha_mode = material_alpha_mode::opaque;

    material_texture base_color_texture;
    material_texture metallic_roughness_texture;
    material_texture normal_texture;
    material_texture occlusion_texture;
    material_texture emissive_texture;
};

struct material_data
{
    glm::vec4 base_color;

    glm::vec3 emissive;
    float metallic;

    float roughness;
    float normal_scale;
    float occlusion_strength;
    float alpha_cutoff;

    uint32_t base_color_texture;
    uint32_t base_color_sampler;

    uint32_t metallic_roughness_texture;
    uint32_t metallic_roughness_sampler;

    uint32_t normal_texture;
    uint32_t normal_sampler;

    uint32_t occlusion_texture;
    uint32_t occlusion_sampler;

    uint32_t emissive_texture;
    uint32_t emissive_sampler;

    uint32_t alpha_mode;
    uint32_t padding;
};

struct material
{
    const shader* target_shader = nullptr;
    uint32_t index = 0;
};

}

namespace glaze::rendering::scene_render::res::materials
{

const material* build(const material_create_info& create_info);

void erase(const material* target);
void erase_all();

const std::vector<material_data>& get_data();

}
