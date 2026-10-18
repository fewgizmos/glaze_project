#include "glp/glaze/world/resources/textured_plane.h"

#include "glp/glaze/rendering/scene_render/resources/shaders_stdlib.h"

#include "glp/glaze/rendering/vk/samplers.h"

#include "glp/glaze/utilities/core/assert.h"

#include <stb_image.h>

#include <glm/vec4.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>

namespace glaze::world::res
{

namespace
{

constexpr int PIXEL_SIZE = 4;

constexpr int RED_CHANNEL   = 0;
constexpr int GREEN_CHANNEL = 1;
constexpr int ALPHA_CHANNEL = 3;

constexpr uint8_t MAX_CHANNEL = 255;

constexpr float DEFAULT_TILE_SIZE = 1.0f;
constexpr float PLAIN_TILE_SIZE   = 8.0f;
constexpr float DEFAULT_ROUGHNESS = 0.8f;

constexpr uint32_t FULL_MIP_CHAIN = 0;

struct pack_files
{
    std::string base_color;
    std::string normal;
    std::string roughness;
    std::string ambient_occlusion;
    std::string cavity;
    std::string transmission;
    std::string displacement;
    std::string metadata;
};

struct image_data
{
    std::vector<uint8_t> pixels;

    int width  = 0;
    int height = 0;
};

std::string to_lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char symbol) { return static_cast<char>(std::tolower(symbol)); });

    return text;
}

bool has_suffix(const std::string& text, const std::string& suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

pack_files find_pack_files(const std::string& pack_path)
{
    pack_files files;

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(pack_path)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::string path      = entry.path().string();
        const std::string stem      = to_lower(entry.path().stem().string());
        const std::string extension = to_lower(entry.path().extension().string());

        if (extension == ".json") {
            files.metadata = path;
        } else if (has_suffix(stem, "_basecolor")) {
            files.base_color = path;
        } else if (has_suffix(stem, "_normal")) {
            files.normal = path;
        } else if (has_suffix(stem, "_roughness")) {
            files.roughness = path;
        } else if (has_suffix(stem, "_ao")) {
            files.ambient_occlusion = path;
        } else if (has_suffix(stem, "_cavity")) {
            files.cavity = path;
        } else if (has_suffix(stem, "_transmission")) {
            files.transmission = path;
        } else if (has_suffix(stem, "_displacement")) {
            files.displacement = path;
        }
    }

    return files;
}

std::string metadata_value(const std::string& json, const std::string& key)
{
    const std::regex pattern("\"key\"\\s*:\\s*\"" + key + "\"[^}]*?\"value\"\\s*:\\s*\"([^\"]*)\"");

    std::smatch match;

    if (!std::regex_search(json, match, pattern)) {
        return {};
    }

    return match[1].str();
}

void read_metadata(const std::string& path, glm::vec2& tile_size, float& relief_height)
{
    if (path.empty()) {
        return;
    }

    std::ifstream file(path);
    std::stringstream stream;
    stream << file.rdbuf();

    const std::string json = stream.str();

    const std::string area = metadata_value(json, "scanArea");

    float width  = 0.0f;
    float height = 0.0f;

    if (std::sscanf(area.c_str(), "%fx%f", &width, &height) == 2 && width > 0.0f && height > 0.0f) {
        tile_size = glm::vec2(width, height);
    }

    float relief = 0.0f;

    if (std::sscanf(metadata_value(json, "height").c_str(), "%f", &relief) == 1 && relief > 0.0f) {
        relief_height = relief;
    }
}

image_data load_image(const std::string& path)
{
    image_data result;

    int channels = 0;

    stbi_uc* pixels = stbi_load(path.c_str(), &result.width, &result.height, &channels, PIXEL_SIZE);

    GLAZE_ASSERT(pixels == nullptr, "failed to load texture: " + path);

    result.pixels.assign(pixels, pixels + static_cast<size_t>(result.width) * result.height * PIXEL_SIZE);

    stbi_image_free(pixels);

    return result;
}

const rendering::scene_render::texture* upload_image(const image_data& image, bool is_srgb)
{
    return rendering::scene_render::res::textures::build_raw(
        image.pixels.data(),
        static_cast<uint32_t>(image.width),
        static_cast<uint32_t>(image.height),
        is_srgb,
        FULL_MIP_CHAIN
    );
}

void flip_green(image_data& image)
{
    for (size_t i = GREEN_CHANNEL; i < image.pixels.size(); i += PIXEL_SIZE) {
        image.pixels[i] = MAX_CHANNEL - image.pixels[i];
    }
}

void multiply_cavity(image_data& occlusion, const image_data& cavity)
{
    if (occlusion.width != cavity.width || occlusion.height != cavity.height) {
        return;
    }

    for (size_t i = 0; i < occlusion.pixels.size(); i++) {
        occlusion.pixels[i] = static_cast<uint8_t>(occlusion.pixels[i] * cavity.pixels[i] / MAX_CHANNEL);
    }
}

void move_red_to_alpha(image_data& image)
{
    for (size_t i = 0; i < image.pixels.size(); i += PIXEL_SIZE) {
        image.pixels[i + ALPHA_CHANNEL] = image.pixels[i + RED_CHANNEL];
    }
}

}

cmp_textured_plane::cmp_textured_plane(const textured_plane_create_info& create_info)
{
    set_flag(scene::vol::component_flags::modeled);

    const pack_files files = create_info.pack_path.empty() ? pack_files{} : find_pack_files(create_info.pack_path);

    glm::vec2 tile_size  = glm::vec2(create_info.pack_path.empty() ? PLAIN_TILE_SIZE : DEFAULT_TILE_SIZE);
    float relief_height  = 0.0f;

    read_metadata(files.metadata, tile_size, relief_height);

    if (create_info.tile_size.x > 0.0f && create_info.tile_size.y > 0.0f) {
        tile_size = create_info.tile_size;
    }

    const uint32_t sampler = rendering::vk::samplers::make(rendering::sampler_create_info{});

    rendering::scene_render::material_create_info material_info;
    material_info.target_shader = rendering::scene_render::std_base_model_shader();
    material_info.metallic      = 0.0f;
    material_info.roughness     = DEFAULT_ROUGHNESS;

    const auto bind = [&](rendering::scene_render::material_texture& slot, const image_data& image, bool is_srgb) {
        const rendering::scene_render::texture* created = upload_image(image, is_srgb);

        m_textures.push_back(created);

        slot.target        = created;
        slot.sampler_index = sampler;
    };

    if (!files.base_color.empty()) {
        bind(material_info.base_color_texture, load_image(files.base_color), true);
    }

    if (!files.normal.empty()) {
        image_data normal = load_image(files.normal);

        if (create_info.flip_normal_y) {
            flip_green(normal);
        }

        bind(material_info.normal_texture, normal, false);
    }

    if (!files.roughness.empty()) {
        material_info.roughness = 1.0f;

        bind(material_info.metallic_roughness_texture, load_image(files.roughness), false);
    }

    if (!files.ambient_occlusion.empty()) {
        image_data occlusion = load_image(files.ambient_occlusion);

        if (!files.cavity.empty()) {
            multiply_cavity(occlusion, load_image(files.cavity));
        }

        bind(material_info.occlusion_texture, occlusion, false);
    }

    if (!files.transmission.empty()) {
        image_data transmission = load_image(files.transmission);

        move_red_to_alpha(transmission);

        material_info.diffuse_transmission_factor = create_info.transmission_factor;

        bind(material_info.diffuse_transmission_texture, transmission, false);
    }

    if (!files.displacement.empty() && relief_height > 0.0f) {
        material_info.height_scale = relief_height * create_info.parallax_strength / (0.5f * (tile_size.x + tile_size.y));
        material_info.height_world = relief_height * create_info.parallax_strength;

        bind(material_info.height_texture, load_image(files.displacement), false);
    }

    m_material = rendering::scene_render::res::materials::build(material_info);

    const float half_x = create_info.size_x * 0.5f;
    const float half_z = create_info.size_z * 0.5f;

    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec4 color(1.0f);

    const auto corner = [&](float x, float z) {
        return rendering::scene_render::vertex{ glm::vec3(x, 0.0f, z), x / tile_size.x, up, -z / tile_size.y, tangent, color };
    };

    const std::vector<rendering::scene_render::vertex> vertices = {
        corner(-half_x, -half_z),
        corner(-half_x,  half_z),
        corner( half_x,  half_z),
        corner( half_x, -half_z)
    };

    const std::vector<uint32_t> indices = { 0, 1, 2, 0, 2, 3 };

    m_mesh = rendering::scene_render::res::meshes::build(vertices, indices);
}

cmp_textured_plane::~cmp_textured_plane()
{
    rendering::scene_render::res::materials::erase(m_material);
    rendering::scene_render::res::meshes::erase(m_mesh);

    for (const rendering::scene_render::texture* target : m_textures) {
        rendering::scene_render::res::textures::erase(target);
    }
}

void cmp_textured_plane::get_model(rendering::scene_render::gpu_model& out)
{
    out.parts.push_back({ m_mesh, m_material, glm::mat4(1.0f), glm::mat4(1.0f) });
}

textured_plane::textured_plane(const textured_plane_create_info& create_info)
{
    m_scene_volume().w_pos = create_info.position;
    m_scene_volume().w_rot = glm::vec3(0.0f, create_info.rotation, 0.0f);

    m_scene_volume().components.push_back(std::make_unique<cmp_textured_plane>(create_info));
}

}
