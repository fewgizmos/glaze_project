#define STB_IMAGE_IMPLEMENTATION

#include "glp/glaze/rendering/scene_render/resources/textures.h"

#include "glp/glaze/rendering/defines.h"

#include "glp/glaze/utilities/core/assert.h"

#include <stb_image.h>

#include <algorithm>
#include <memory>
#include <unordered_map>

namespace glaze::rendering::scene_render::resources::textures
{

namespace
{

struct erased_texture
{
    texture target;
    int frames_left = 0;
};

constexpr uint32_t PIXEL_SIZE = 4;

std::unordered_map<const texture*, std::unique_ptr<texture>> m_textures;

std::vector<erased_texture> m_erased;

std::vector<const texture*> m_built;

std::vector<uint32_t> m_free_indices;

uint32_t m_next_index = 0;

uint32_t take_index()
{
    if (!m_free_indices.empty()) {
        const uint32_t index = m_free_indices.back();

        m_free_indices.pop_back();

        return index;
    }

    GLAZE_ASSERT(m_next_index >= MAX_TEXTURES, "textures limit exceeded");

    return m_next_index++;
}

uint32_t get_max_mip_levels(uint32_t width, uint32_t height)
{
    uint32_t levels = 1;

    for (uint32_t size = std::max(width, height); size > 1; size /= 2) {
        levels++;
    }

    return levels;
}

}

const texture* build_raw(const void* pixels, uint32_t width, uint32_t height, bool is_srgb, uint32_t mip_levels)
{
    const uint32_t max_mip_levels = get_max_mip_levels(width, height);

    std::unique_ptr<texture> created = std::make_unique<texture>();

    created->index = take_index();

    created->target = vk::resources::create_image(
        { width, height, 1 },
        is_srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        mip_levels == 0 ? max_mip_levels : std::min(mip_levels, max_mip_levels)
    );

    vk::resources::queue_image_upload(
        created->target,
        pixels,
        static_cast<VkDeviceSize>(width) * height * PIXEL_SIZE
    );

    const texture* result = created.get();

    m_textures.emplace(result, std::move(created));

    m_built.push_back(result);

    return result;
}

const texture* build_stb(const std::string& path, bool is_srgb, uint32_t mip_levels)
{
    int width    = 0;
    int height   = 0;
    int channels = 0;

    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, PIXEL_SIZE);

    GLAZE_ASSERT(pixels == nullptr, "failed to load texture: " + path);

    const texture* result = build_raw(
        pixels,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        is_srgb,
        mip_levels
    );

    stbi_image_free(pixels);

    return result;
}

void erase(const texture* target)
{
    const auto found = m_textures.find(target);

    if (found == m_textures.end()) {
        return;
    }

    m_erased.push_back({ *found->second, MAX_FRAMES_IN_FLIGHT });

    m_built.erase(std::remove(m_built.begin(), m_built.end(), target), m_built.end());

    m_textures.erase(found);
}

void erase_all()
{
    for (auto& entry : m_textures) {
        vk::resources::destroy_image(entry.second->target);
    }

    for (erased_texture& erased : m_erased) {
        vk::resources::destroy_image(erased.target.target);
    }

    m_textures.clear();
    m_erased.clear();
    m_built.clear();

    m_free_indices.clear();

    m_next_index = 0;
}

void destroy_erased()
{
    for (auto it = m_erased.begin(); it != m_erased.end();) {
        if (it->frames_left > 0) {
            it->frames_left--;
            it++;

            continue;
        }

        m_free_indices.push_back(it->target.index);

        vk::resources::destroy_image(it->target.target);

        it = m_erased.erase(it);
    }
}

std::vector<const texture*> take_built()
{
    std::vector<const texture*> built = std::move(m_built);

    m_built.clear();

    return built;
}

}
