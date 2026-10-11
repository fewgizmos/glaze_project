#include "glp/glaze/rendering/scene_render/resources/models.h"

#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/defines.h"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <stb_image.h>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <utility>
#include <variant>

namespace glaze::rendering::scene_render::res::models
{

namespace
{

struct stored_model
{
    std::unique_ptr<model> target;

    bool is_marked = false;
    int frames_left = 0;
};

constexpr const char* DEFAULT_SHADER_NAME = "base_model";

constexpr int PIXEL_CHANNELS = 4;

std::vector<stored_model> m_models;

void destroy_resources(const model& target)
{
    for (const material* owned : target.materials) {
        materials::erase(owned);
    }

    for (const texture* owned : target.textures) {
        textures::erase(owned);
    }

    for (const mesh* owned : target.meshes) {
        meshes::erase(owned);
    }
}

const texture* build_encoded_texture(const std::byte* bytes, size_t size, bool is_srgb)
{
    int width    = 0;
    int height   = 0;
    int channels = 0;

    stbi_uc* pixels = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(bytes),
        static_cast<int>(size),
        &width,
        &height,
        &channels,
        PIXEL_CHANNELS
    );

    const texture* result = textures::build_raw(
        pixels,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        is_srgb,
        0
    );

    stbi_image_free(pixels);

    return result;
}

const texture* build_buffer_view_texture(const fastgltf::Asset& asset, size_t view_index, bool is_srgb)
{
    const fastgltf::BufferView& view = asset.bufferViews[view_index];

    const texture* result = nullptr;

    std::visit(fastgltf::visitor {
        [&](const fastgltf::sources::Array& array) {
            result = build_encoded_texture(array.bytes.data() + view.byteOffset, view.byteLength, is_srgb);
        },
        [&](const fastgltf::sources::ByteView& bytes) {
            result = build_encoded_texture(bytes.bytes.data() + view.byteOffset, view.byteLength, is_srgb);
        },
        [](const auto&) {}
    }, asset.buffers[view.bufferIndex].data);

    return result;
}

const texture* build_image_texture(
    const fastgltf::Asset& asset,
    const fastgltf::Image& source,
    const std::filesystem::path& directory,
    bool is_srgb
)
{
    const texture* result = nullptr;

    std::visit(fastgltf::visitor {
        [&](const fastgltf::sources::URI& file) {
            result = textures::build_from_file((directory / file.uri.fspath()).string(), is_srgb, 0);
        },
        [&](const fastgltf::sources::Array& array) {
            result = build_encoded_texture(array.bytes.data(), array.bytes.size_bytes(), is_srgb);
        },
        [&](const fastgltf::sources::BufferView& view) {
            result = build_buffer_view_texture(asset, view.bufferViewIndex, is_srgb);
        },
        [](const auto&) {}
    }, source.data);

    return result;
}

uint32_t make_sampler(const fastgltf::Asset& asset, const fastgltf::Texture& source)
{
    sampler_create_info create_info;

    if (!source.samplerIndex.has_value()) {
        return vk::samplers::make(create_info);
    }

    const fastgltf::Sampler& sampler = asset.samplers[source.samplerIndex.value()];

    if (sampler.magFilter.has_value() && sampler.magFilter.value() == fastgltf::Filter::Nearest) {
        create_info.mag_filter = VK_FILTER_NEAREST;
    }

    if (sampler.minFilter.has_value()) {
        switch (sampler.minFilter.value()) {
            case fastgltf::Filter::Nearest:
            case fastgltf::Filter::NearestMipMapLinear:
                create_info.min_filter = VK_FILTER_NEAREST;
                break;

            case fastgltf::Filter::NearestMipMapNearest:
                create_info.min_filter = VK_FILTER_NEAREST;
                create_info.mip_mode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                break;

            case fastgltf::Filter::LinearMipMapNearest:
                create_info.mip_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                break;

            default:
                break;
        }
    }

    const auto get_wrap = [](fastgltf::Wrap wrap) {
        switch (wrap) {
            case fastgltf::Wrap::ClampToEdge:    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case fastgltf::Wrap::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case fastgltf::Wrap::Repeat:         return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }

        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    };

    create_info.wrap_u = get_wrap(sampler.wrapS);
    create_info.wrap_v = get_wrap(sampler.wrapT);

    return vk::samplers::make(create_info);
}

void generate_normals(std::vector<vertex>& vertices, const std::vector<uint32_t>& indices)
{
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        vertex& a = vertices[indices[i]];
        vertex& b = vertices[indices[i + 1]];
        vertex& c = vertices[indices[i + 2]];

        const glm::vec3 face_normal = glm::cross(b.pos - a.pos, c.pos - a.pos);

        a.normal += face_normal;
        b.normal += face_normal;
        c.normal += face_normal;
    }

    for (vertex& point : vertices) {
        const float length = glm::length(point.normal);

        point.normal = length > 0.0f ? point.normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

void generate_tangents(std::vector<vertex>& vertices, const std::vector<uint32_t>& indices)
{
    std::vector<glm::vec3> tangents(vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitangents(vertices.size(), glm::vec3(0.0f));

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t ia = indices[i];
        const uint32_t ib = indices[i + 1];
        const uint32_t ic = indices[i + 2];

        const vertex& a = vertices[ia];
        const vertex& b = vertices[ib];
        const vertex& c = vertices[ic];

        const glm::vec3 edge_ab = b.pos - a.pos;
        const glm::vec3 edge_ac = c.pos - a.pos;

        const glm::vec2 uv_ab(b.uv_x - a.uv_x, b.uv_y - a.uv_y);
        const glm::vec2 uv_ac(c.uv_x - a.uv_x, c.uv_y - a.uv_y);

        const float determinant = uv_ab.x * uv_ac.y - uv_ac.x * uv_ab.y;

        if (std::abs(determinant) < 1e-12f) {
            continue;
        }

        const glm::vec3 tangent   = (edge_ab * uv_ac.y - edge_ac * uv_ab.y) / determinant;
        const glm::vec3 bitangent = (edge_ac * uv_ab.x - edge_ab * uv_ac.x) / determinant;

        tangents[ia] += tangent;
        tangents[ib] += tangent;
        tangents[ic] += tangent;

        bitangents[ia] += bitangent;
        bitangents[ib] += bitangent;
        bitangents[ic] += bitangent;
    }

    for (size_t i = 0; i < vertices.size(); i++) {
        const glm::vec3 normal = vertices[i].normal;

        glm::vec3 tangent = tangents[i] - normal * glm::dot(normal, tangents[i]);

        if (glm::length(tangent) < 1e-6f) {
            const glm::vec3 axis = std::abs(normal.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

            tangent = glm::cross(axis, normal);
        }

        tangent = glm::normalize(tangent);

        const float handedness = glm::dot(glm::cross(normal, tangent), bitangents[i]) < 0.0f ? -1.0f : 1.0f;

        vertices[i].tangent = glm::vec4(tangent, handedness);
    }
}

const mesh* build_primitive_mesh(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive)
{
    const fastgltf::Accessor& position_accessor = asset.accessors[primitive.findAttribute("POSITION")->accessorIndex];

    vertex empty = {};
    empty.color = glm::vec4(1.0f);

    std::vector<vertex> vertices(position_accessor.count, empty);
    std::vector<uint32_t> indices;

    fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, position_accessor, [&](glm::vec3 value, size_t index) {
        vertices[index].pos = value;
    });

    const auto* normal = primitive.findAttribute("NORMAL");
    const auto* tangent = primitive.findAttribute("TANGENT");
    const auto* uv = primitive.findAttribute("TEXCOORD_0");
    const auto* color = primitive.findAttribute("COLOR_0");

    const bool has_normals  = normal != primitive.attributes.cend();
    const bool has_tangents = tangent != primitive.attributes.cend();

    if (has_normals) {
        fastgltf::iterateAccessorWithIndex<glm::vec3>(
            asset,
            asset.accessors[normal->accessorIndex],
            [&](glm::vec3 value, size_t index) { vertices[index].normal = value; }
        );
    }

    if (has_tangents) {
        fastgltf::iterateAccessorWithIndex<glm::vec4>(
            asset,
            asset.accessors[tangent->accessorIndex],
            [&](glm::vec4 value, size_t index) { vertices[index].tangent = value; }
        );
    }

    if (uv != primitive.attributes.cend()) {
        fastgltf::iterateAccessorWithIndex<glm::vec2>(
            asset,
            asset.accessors[uv->accessorIndex],
            [&](glm::vec2 value, size_t index) {
                vertices[index].uv_x = value.x;
                vertices[index].uv_y = value.y;
            }
        );
    }

    if (color != primitive.attributes.cend()) {
        const fastgltf::Accessor& color_accessor = asset.accessors[color->accessorIndex];

        if (color_accessor.type == fastgltf::AccessorType::Vec3) {
            fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, color_accessor, [&](glm::vec3 value, size_t index) {
                vertices[index].color = glm::vec4(value, 1.0f);
            });
        } else {
            fastgltf::iterateAccessorWithIndex<glm::vec4>(asset, color_accessor, [&](glm::vec4 value, size_t index) {
                vertices[index].color = value;
            });
        }
    }

    if (primitive.indicesAccessor.has_value()) {
        const fastgltf::Accessor& index_accessor = asset.accessors[primitive.indicesAccessor.value()];

        indices.resize(index_accessor.count);

        fastgltf::iterateAccessorWithIndex<uint32_t>(asset, index_accessor, [&](uint32_t value, size_t index) {
            indices[index] = value;
        });
    } else {
        indices.resize(vertices.size());

        for (uint32_t i = 0; i < indices.size(); i++) {
            indices[i] = i;
        }
    }

    if (!has_normals) {
        generate_normals(vertices, indices);
    }

    if (!has_tangents) {
        generate_tangents(vertices, indices);
    }

    return meshes::build(vertices, indices);
}

std::unique_ptr<model> build_gltf(const std::string& gltf_path, const shader* target_shader)
{
    const std::filesystem::path directory = std::filesystem::path(gltf_path).parent_path();

    fastgltf::Parser parser;

    auto data = fastgltf::GltfDataBuffer::FromPath(gltf_path);

    auto loaded = parser.loadGltf(data.get(), directory, fastgltf::Options::LoadExternalBuffers);

    const fastgltf::Asset& asset = loaded.get();

    std::unique_ptr<model> created = std::make_unique<model>();

    created->path          = gltf_path;
    created->target_shader = target_shader;

    std::vector<bool> image_is_srgb(asset.images.size(), false);

    for (const fastgltf::Material& source : asset.materials) {
        if (source.pbrData.baseColorTexture.has_value()) {
            const fastgltf::Texture& used = asset.textures[source.pbrData.baseColorTexture.value().textureIndex];

            if (used.imageIndex.has_value()) {
                image_is_srgb[used.imageIndex.value()] = true;
            }
        }

        if (source.emissiveTexture.has_value()) {
            const fastgltf::Texture& used = asset.textures[source.emissiveTexture.value().textureIndex];

            if (used.imageIndex.has_value()) {
                image_is_srgb[used.imageIndex.value()] = true;
            }
        }
    }

    std::vector<const texture*> image_textures;

    for (size_t i = 0; i < asset.images.size(); i++) {
        const texture* built = build_image_texture(asset, asset.images[i], directory, image_is_srgb[i]);

        image_textures.push_back(built);

        if (built != nullptr) {
            created->textures.push_back(built);
        }
    }

    const auto get_material_texture = [&](size_t texture_index) {
        const fastgltf::Texture& source = asset.textures[texture_index];

        material_texture result;

        if (source.imageIndex.has_value()) {
            result.target = image_textures[source.imageIndex.value()];
        }

        result.sampler_index = make_sampler(asset, source);

        return result;
    };

    std::vector<const material*> gltf_materials;

    for (const fastgltf::Material& source : asset.materials) {
        material_create_info create_info;

        create_info.target_shader = target_shader;

        create_info.base_color = glm::vec4(
            source.pbrData.baseColorFactor[0],
            source.pbrData.baseColorFactor[1],
            source.pbrData.baseColorFactor[2],
            source.pbrData.baseColorFactor[3]
        );

        create_info.emissive = glm::vec3(source.emissiveFactor[0], source.emissiveFactor[1], source.emissiveFactor[2]);

        create_info.metallic  = source.pbrData.metallicFactor;
        create_info.roughness = source.pbrData.roughnessFactor;

        create_info.alpha_cutoff = source.alphaCutoff;

        if (source.pbrData.baseColorTexture.has_value()) {
            create_info.base_color_texture = get_material_texture(source.pbrData.baseColorTexture.value().textureIndex);
        }

        if (source.pbrData.metallicRoughnessTexture.has_value()) {
            create_info.metallic_roughness_texture = get_material_texture(
                source.pbrData.metallicRoughnessTexture.value().textureIndex
            );
        }

        if (source.normalTexture.has_value()) {
            create_info.normal_texture = get_material_texture(source.normalTexture.value().textureIndex);
            create_info.normal_scale   = source.normalTexture.value().scale;
        }

        if (source.occlusionTexture.has_value()) {
            create_info.occlusion_texture  = get_material_texture(source.occlusionTexture.value().textureIndex);
            create_info.occlusion_strength = source.occlusionTexture.value().strength;
        }

        if (source.emissiveTexture.has_value()) {
            create_info.emissive_texture = get_material_texture(source.emissiveTexture.value().textureIndex);
        }

        const material* built = materials::build(create_info);

        gltf_materials.push_back(built);

        created->materials.push_back(built);
    }

    const material* default_material = nullptr;

    std::vector<std::vector<model_part>> mesh_parts(asset.meshes.size());

    for (size_t i = 0; i < asset.meshes.size(); i++) {
        for (const fastgltf::Primitive& primitive : asset.meshes[i].primitives) {
            if (primitive.type != fastgltf::PrimitiveType::Triangles) {
                continue;
            }

            model_part part;

            part.target_mesh = build_primitive_mesh(asset, primitive);

            created->meshes.push_back(part.target_mesh);

            if (primitive.materialIndex.has_value()) {
                part.target_material = gltf_materials[primitive.materialIndex.value()];
            } else {
                if (default_material == nullptr) {
                    material_create_info create_info;

                    create_info.target_shader = target_shader;

                    default_material = materials::build(create_info);

                    created->materials.push_back(default_material);
                }

                part.target_material = default_material;
            }

            mesh_parts[i].push_back(part);
        }
    }

    std::vector<bool> has_parent(asset.nodes.size(), false);

    for (const fastgltf::Node& source : asset.nodes) {
        for (const size_t child : source.children) {
            has_parent[child] = true;
        }
    }

    std::vector<std::pair<size_t, int>> pending;

    for (size_t i = asset.nodes.size(); i > 0; i--) {
        if (!has_parent[i - 1]) {
            pending.push_back({ i - 1, -1 });
        }
    }

    while (!pending.empty()) {
        const std::pair<size_t, int> current = pending.back();

        pending.pop_back();

        const fastgltf::Node& source = asset.nodes[current.first];

        model_node node;

        node.parent = current.second;
        node.local  = glm::make_mat4(fastgltf::getTransformMatrix(source).data());

        if (source.meshIndex.has_value()) {
            node.parts = mesh_parts[source.meshIndex.value()];
        }

        created->nodes.push_back(node);

        const int index = static_cast<int>(created->nodes.size() - 1);

        for (const size_t child : source.children) {
            pending.push_back({ child, index });
        }
    }

    return created;
}

}

const model* make(const std::string& gltf_path, const shader* target_shader)
{
    if (target_shader == nullptr) {
        target_shader = shaders::get(DEFAULT_SHADER_NAME);
    }

    for (stored_model& stored : m_models) {
        if (stored.target->path != gltf_path || stored.target->target_shader != target_shader) {
            continue;
        }

        stored.is_marked = false;

        stored.target->references++;

        return stored.target.get();
    }

    stored_model stored;

    stored.target = build_gltf(gltf_path, target_shader);

    stored.target->references = 1;

    m_models.push_back(std::move(stored));

    return m_models.back().target.get();
}

void set_unused(const model* target)
{
    for (stored_model& stored : m_models) {
        if (stored.target.get() != target) {
            continue;
        }

        if (stored.target->references > 0) {
            stored.target->references--;
        }

        return;
    }
}

void erase_unused()
{
    for (auto it = m_models.begin(); it != m_models.end();) {
        if (it->target->references > 0) {
            it++;

            continue;
        }

        if (!it->is_marked) {
            it->is_marked   = true;
            it->frames_left = MAX_FRAMES_IN_FLIGHT;

            it++;

            continue;
        }

        if (it->frames_left > 0) {
            it->frames_left--;
            it++;

            continue;
        }

        destroy_resources(*it->target);

        it = m_models.erase(it);
    }
}

void erase_all()
{
    for (const stored_model& stored : m_models) {
        destroy_resources(*stored.target);
    }

    m_models.clear();
}

}
