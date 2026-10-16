#include "glp/glaze/world/resources/cubemap.h"

#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"
#include "glp/glaze/rendering/queue_deletor.h"

#include "glp/glaze/utilities/core/assert.h"

#include <stb_image.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace glaze::world::res
{

namespace
{

constexpr uint32_t FACES_COUNT = 6;
constexpr uint32_t PIXEL_SIZE  = 4;

constexpr const char* FACE_NAMES[FACES_COUNT] = { "px", "nx", "py", "ny", "pz", "nz" };

VkDescriptorSetLayout cubemap_set_layout()
{
    static VkDescriptorSetLayout layout = [] {
        const VkDescriptorSetLayout created = rendering::vk::descriptors::create_set_layout({
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
        });

        rendering::queue_deletor::push([created]() { rendering::vk::descriptors::destroy_set_layout(created); });

        return created;
    }();

    return layout;
}

const rendering::scene_render::shader* cubemap_shader()
{
    static const rendering::scene_render::shader* shd = [] {
        rendering::graphics_pipeline_create_info info;
        info.color_format         = rendering::HDR_FORMAT;
        info.depth_format         = rendering::DEPTH_FORMAT;
        info.cull_mode            = VK_CULL_MODE_NONE;
        info.depth_write          = false;
        info.set_layouts          = { rendering::scene_render::it::get_scene_set_layout(), cubemap_set_layout() };
        info.push_constant_size   = sizeof(VkDeviceAddress);
        info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        return rendering::scene_render::res::shaders::build("cubemap", "cubemap.vert", "cubemap.frag", "", info, {}, rendering::scene_render::render_queue::skybox);
    }();

    return shd;
}

}

cmp_cubemap::cmp_cubemap(const std::string& dir, float intensity)
{
    set_flag(scene::vol::component_flags::modeled);

    std::vector<stbi_uc> pixels;

    uint32_t size = 0;

    for (uint32_t face = 0; face < FACES_COUNT; face++) {
        const std::string path = dir + "/" + FACE_NAMES[face] + ".png";

        int width    = 0;
        int height   = 0;
        int channels = 0;

        stbi_uc* face_pixels = stbi_load(path.c_str(), &width, &height, &channels, PIXEL_SIZE);

        GLAZE_ASSERT(face_pixels == nullptr, "failed to load cubemap face: " + path);
        GLAZE_ASSERT(width != height, "cubemap face is not square: " + path);
        GLAZE_ASSERT(face != 0 && static_cast<uint32_t>(width) != size, "cubemap faces have different sizes: " + path);

        size = static_cast<uint32_t>(width);

        const size_t face_bytes = static_cast<size_t>(size) * size * PIXEL_SIZE;

        pixels.resize(face_bytes * (face + 1));

        std::memcpy(pixels.data() + face_bytes * face, face_pixels, face_bytes);

        stbi_image_free(face_pixels);
    }

    m_image = rendering::vk::resources::create_image(
        { size, size, 1 },
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1,
        FACES_COUNT,
        true
    );

    rendering::vk::resources::queue_image_upload(m_image, pixels.data(), pixels.size());

    rendering::sampler_create_info sampler_info;
    sampler_info.wrap_u     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_v     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_w     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropy = false;

    const VkSampler sampler = rendering::vk::samplers::get(rendering::vk::samplers::make(sampler_info));

    m_pool = rendering::vk::descriptors::create_pool(1, { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 } });
    m_set  = rendering::vk::descriptors::allocate_set(m_pool, cubemap_set_layout());

    rendering::vk::descriptors::write_image(
        m_set,
        0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        m_image.view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        sampler
    );

    const std::vector<rendering::scene_render::vertex> vertices(3, rendering::scene_render::vertex{});
    const std::vector<uint32_t> indices = { 0, 1, 2 };

    m_mesh = rendering::scene_render::res::meshes::build(vertices, indices);

    rendering::scene_render::material_create_info create_info;
    create_info.target_shader = cubemap_shader();
    create_info.extra_set     = m_set;
    create_info.base_color    = glm::vec4(glm::vec3(intensity), 1.0f);

    m_material = rendering::scene_render::res::materials::build(create_info);
}

cmp_cubemap::~cmp_cubemap()
{
    rendering::vk::device::get_disp().deviceWaitIdle();

    rendering::scene_render::res::materials::erase(m_material);
    rendering::scene_render::res::meshes::erase(m_mesh);

    rendering::vk::descriptors::destroy_pool(m_pool);

    rendering::vk::resources::destroy_image(m_image);
}

rendering::scene_render::gpu_model cmp_cubemap::get_model()
{
    rendering::scene_render::gpu_model result;

    result.parts.push_back({ m_mesh, m_material, glm::mat4(1.0f) });

    return result;
}

cubemap::cubemap(const std::string& dir, float intensity)
{
    m_scene_volume().components.push_back(std::make_unique<cmp_cubemap>(dir, intensity));
}

}
