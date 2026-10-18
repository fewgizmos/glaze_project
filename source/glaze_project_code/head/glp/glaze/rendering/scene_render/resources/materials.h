#pragma once

#include "glp/glaze/rendering/scene_render/resources/shaders.h"
#include "glp/glaze/rendering/scene_render/resources/textures.h"

#include <glm/vec2.hpp>
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

constexpr uint32_t MATERIAL_TEXTURES_COUNT = 14;

struct material_texture
{
    const texture* target = nullptr;
    uint32_t sampler_index = 0;

    glm::vec2 uv_offset = glm::vec2(0.0f);
    glm::vec2 uv_scale  = glm::vec2(1.0f);
    float uv_rotation   = 0.0f;
};

struct material_create_info
{
    const shader* target_shader = nullptr;

    VkDescriptorSet extra_set = VK_NULL_HANDLE;

    glm::vec4 base_color = glm::vec4(1.0f);
    glm::vec3 emissive   = glm::vec3(0.0f);

    float metallic  = 1.0f;
    float roughness = 1.0f;

    float normal_scale       = 1.0f;
    float occlusion_strength = 1.0f;
    float alpha_cutoff       = 0.5f;

    material_alpha_mode alpha_mode = material_alpha_mode::opaque;

    bool double_sided = false;
    bool cast_shadows = true;

    float ior               = 1.5f;
    float specular_factor   = 1.0f;
    float emissive_strength = 1.0f;

    glm::vec3 specular_color = glm::vec3(1.0f);

    float diffuse_transmission_factor = 0.0f;

    glm::vec3 diffuse_transmission_color = glm::vec3(1.0f);

    float clearcoat_factor       = 0.0f;
    float clearcoat_roughness    = 0.0f;
    float clearcoat_normal_scale = 1.0f;

    glm::vec3 sheen_color = glm::vec3(0.0f);
    float sheen_roughness = 0.0f;

    material_texture base_color_texture;
    material_texture metallic_roughness_texture;
    material_texture normal_texture;
    material_texture occlusion_texture;
    material_texture emissive_texture;
    material_texture specular_texture;
    material_texture specular_color_texture;
    material_texture diffuse_transmission_texture;
    material_texture diffuse_transmission_color_texture;
    material_texture clearcoat_texture;
    material_texture clearcoat_roughness_texture;
    material_texture clearcoat_normal_texture;
    material_texture sheen_color_texture;
    material_texture sheen_roughness_texture;

    material_texture height_texture;
    float height_scale = 0.0f;
    float height_world = 0.0f;
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

    uint32_t specular_texture;
    uint32_t specular_sampler;

    uint32_t specular_color_texture;
    uint32_t specular_color_sampler;

    uint32_t diffuse_transmission_texture;
    uint32_t diffuse_transmission_sampler;

    uint32_t diffuse_transmission_color_texture;
    uint32_t diffuse_transmission_color_sampler;

    uint32_t clearcoat_texture;
    uint32_t clearcoat_sampler;

    uint32_t clearcoat_roughness_texture;
    uint32_t clearcoat_roughness_sampler;

    uint32_t clearcoat_normal_texture;
    uint32_t clearcoat_normal_sampler;

    uint32_t sheen_color_texture;
    uint32_t sheen_color_sampler;

    uint32_t sheen_roughness_texture;
    uint32_t sheen_roughness_sampler;

    uint32_t height_texture;
    uint32_t height_sampler;
    uint32_t padding0;

    glm::vec4 uv_transforms[MATERIAL_TEXTURES_COUNT * 2];

    glm::vec3 specular_color;
    float specular_factor;

    float ior;
    float emissive_strength;
    float clearcoat_factor;
    float clearcoat_roughness;

    glm::vec3 diffuse_transmission_color;
    float diffuse_transmission_factor;

    glm::vec3 sheen_color;
    float sheen_roughness;

    float clearcoat_normal_scale;
    float height_scale;
    float height_world;
    float padding1;
};

struct material
{
    const shader* target_shader = nullptr;
    VkDescriptorSet extra_set = VK_NULL_HANDLE;
    uint32_t index = 0;
    bool double_sided = false;
    bool cast_shadows = true;
    material_alpha_mode alpha_mode = material_alpha_mode::opaque;
};

}

namespace glaze::rendering::scene_render::res::materials
{

const material* build(const material_create_info& create_info);

void erase(const material* target);
void erase_all();

const std::vector<material_data>& get_data();

}
