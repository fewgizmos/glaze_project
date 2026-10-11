#include "glp/glaze/rendering/scene_render/resources/materials.h"

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

}

const material* build(const material_create_info& create_info)
{
    std::unique_ptr<material> created = std::make_unique<material>();

    created->target_shader = create_info.target_shader;
    created->index         = take_index();

    material_data& data = m_data[created->index];

    data.base_color = create_info.base_color;

    data.emissive = create_info.emissive;
    data.metallic = create_info.metallic;

    data.roughness          = create_info.roughness;
    data.normal_scale       = create_info.normal_scale;
    data.occlusion_strength = create_info.occlusion_strength;
    data.alpha_cutoff       = create_info.alpha_cutoff;

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
