#include "glp/glaze/rendering/scene_render/resources/materials.h"

#include <cmath>
#include <memory>
#include <unordered_map>

namespace glaze::rendering::scene_render::res::materials
{

namespace
{

std::unordered_map<const material*, std::unique_ptr<material>> m_materials;

std::vector<material_data> m_data;

std::vector<uint32_t> m_free_indices;

uint32_t take_index()
{
    if (!m_free_indices.empty()) {
        const uint32_t index = m_free_indices.back();

        m_free_indices.pop_back();

        return index;
    }

    m_data.push_back({});

    return static_cast<uint32_t>(m_data.size() - 1);
}

uint32_t get_texture_index(const material_texture& source)
{
    return source.target != nullptr ? source.target->index : NO_TEXTURE;
}

void write_uv_transform(const material_texture& source, glm::vec4& row0, glm::vec4& row1)
{
    const float cos_rotation = std::cos(source.uv_rotation);
    const float sin_rotation = std::sin(source.uv_rotation);

    row0 = glm::vec4(cos_rotation * source.uv_scale.x, -sin_rotation * source.uv_scale.y, source.uv_offset.x, 0.0f);
    row1 = glm::vec4(sin_rotation * source.uv_scale.x,  cos_rotation * source.uv_scale.y, source.uv_offset.y, 0.0f);
}

}

const material* build(const material_create_info& create_info)
{
    std::unique_ptr<material> created = std::make_unique<material>();

    created->target_shader = create_info.target_shader;
    created->extra_set     = create_info.extra_set;
    created->double_sided  = create_info.double_sided;
    created->index         = take_index();

    material_data& data = m_data[created->index];

    data.base_color = create_info.base_color;

    data.emissive = create_info.emissive;
    data.metallic = create_info.metallic;

    data.roughness          = create_info.roughness;
    data.normal_scale       = create_info.normal_scale;
    data.occlusion_strength = create_info.occlusion_strength;
    data.alpha_cutoff       = create_info.alpha_cutoff;

    data.alpha_mode = static_cast<uint32_t>(create_info.alpha_mode);

    data.base_color_texture = get_texture_index(create_info.base_color_texture);
    data.base_color_sampler = create_info.base_color_texture.sampler_index;

    data.metallic_roughness_texture = get_texture_index(create_info.metallic_roughness_texture);
    data.metallic_roughness_sampler = create_info.metallic_roughness_texture.sampler_index;

    data.normal_texture = get_texture_index(create_info.normal_texture);
    data.normal_sampler = create_info.normal_texture.sampler_index;

    data.occlusion_texture = get_texture_index(create_info.occlusion_texture);
    data.occlusion_sampler = create_info.occlusion_texture.sampler_index;

    data.emissive_texture = get_texture_index(create_info.emissive_texture);
    data.emissive_sampler = create_info.emissive_texture.sampler_index;

    data.specular_texture = get_texture_index(create_info.specular_texture);
    data.specular_sampler = create_info.specular_texture.sampler_index;

    data.specular_color_texture = get_texture_index(create_info.specular_color_texture);
    data.specular_color_sampler = create_info.specular_color_texture.sampler_index;

    data.diffuse_transmission_texture = get_texture_index(create_info.diffuse_transmission_texture);
    data.diffuse_transmission_sampler = create_info.diffuse_transmission_texture.sampler_index;

    data.diffuse_transmission_color_texture = get_texture_index(create_info.diffuse_transmission_color_texture);
    data.diffuse_transmission_color_sampler = create_info.diffuse_transmission_color_texture.sampler_index;

    data.clearcoat_texture = get_texture_index(create_info.clearcoat_texture);
    data.clearcoat_sampler = create_info.clearcoat_texture.sampler_index;

    data.clearcoat_roughness_texture = get_texture_index(create_info.clearcoat_roughness_texture);
    data.clearcoat_roughness_sampler = create_info.clearcoat_roughness_texture.sampler_index;

    data.clearcoat_normal_texture = get_texture_index(create_info.clearcoat_normal_texture);
    data.clearcoat_normal_sampler = create_info.clearcoat_normal_texture.sampler_index;

    data.sheen_color_texture = get_texture_index(create_info.sheen_color_texture);
    data.sheen_color_sampler = create_info.sheen_color_texture.sampler_index;

    data.sheen_roughness_texture = get_texture_index(create_info.sheen_roughness_texture);
    data.sheen_roughness_sampler = create_info.sheen_roughness_texture.sampler_index;

    data.padding0[0] = 0;
    data.padding0[1] = 0;
    data.padding0[2] = 0;

    const material_texture* textures[MATERIAL_TEXTURES_COUNT] = {
        &create_info.base_color_texture,
        &create_info.metallic_roughness_texture,
        &create_info.normal_texture,
        &create_info.occlusion_texture,
        &create_info.emissive_texture,
        &create_info.specular_texture,
        &create_info.specular_color_texture,
        &create_info.diffuse_transmission_texture,
        &create_info.diffuse_transmission_color_texture,
        &create_info.clearcoat_texture,
        &create_info.clearcoat_roughness_texture,
        &create_info.clearcoat_normal_texture,
        &create_info.sheen_color_texture,
        &create_info.sheen_roughness_texture
    };

    for (uint32_t i = 0; i < MATERIAL_TEXTURES_COUNT; i++) {
        write_uv_transform(*textures[i], data.uv_transforms[i * 2], data.uv_transforms[i * 2 + 1]);
    }

    data.specular_color  = create_info.specular_color;
    data.specular_factor = create_info.specular_factor;

    data.ior                 = create_info.ior;
    data.emissive_strength   = create_info.emissive_strength;
    data.clearcoat_factor    = create_info.clearcoat_factor;
    data.clearcoat_roughness = create_info.clearcoat_roughness;

    data.diffuse_transmission_color  = create_info.diffuse_transmission_color;
    data.diffuse_transmission_factor = create_info.diffuse_transmission_factor;

    data.sheen_color     = create_info.sheen_color;
    data.sheen_roughness = create_info.sheen_roughness;

    data.clearcoat_normal_scale = create_info.clearcoat_normal_scale;

    data.padding1[0] = 0.0f;
    data.padding1[1] = 0.0f;
    data.padding1[2] = 0.0f;

    const material* result = created.get();

    m_materials.emplace(result, std::move(created));

    return result;
}

void erase(const material* target)
{
    const auto found = m_materials.find(target);

    if (found == m_materials.end()) {
        return;
    }

    m_free_indices.push_back(found->second->index);

    m_materials.erase(found);
}

void erase_all()
{
    m_materials.clear();
    m_data.clear();

    m_free_indices.clear();
}

const std::vector<material_data>& get_data() { return m_data; }

}
