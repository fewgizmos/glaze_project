#include "glp/glaze/world/resources/cubemap.h"

#include "glp/glaze/rendering/scene_render/environment.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"
#include "glp/glaze/rendering/queue_deletor.h"

#include "glp/glaze/utilities/core/assert.h"

#include <stb_image.h>

#include <algorithm>
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

constexpr uint32_t IRRADIANCE_SIZE  = 32;
constexpr uint32_t PREFILTERED_SIZE = 128;
constexpr uint32_t PREFILTERED_MIPS = 5;

constexpr uint32_t BAKE_GROUP = 8;

constexpr VkFormat IBL_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;

constexpr const char* FACE_NAMES[FACES_COUNT] = { "px", "nx", "py", "ny", "pz", "nz" };

struct prefilter_push
{
    float roughness;
    float source_resolution;
};

VkDescriptorSetLayout bake_set_layout()
{
    static VkDescriptorSetLayout layout = [] {
        const VkDescriptorSetLayout created = rendering::vk::descriptors::create_set_layout({
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
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
        info.set_layouts          = { rendering::scene_render::it::get_scene_set_layout(), rendering::scene_render::environment::get_set_layout() };
        info.push_constant_size   = sizeof(VkDeviceAddress);
        info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        return rendering::scene_render::res::shaders::build("cubemap", "cubemap.vert", "cubemap.frag", "", info, {}, rendering::scene_render::render_queue::skybox);
    }();

    return shd;
}

const rendering::scene_render::shader* irradiance_shader()
{
    static const rendering::scene_render::shader* shd = [] {
        rendering::compute_pipeline_create_info info;
        info.set_layouts = { bake_set_layout() };

        return rendering::scene_render::res::shaders::build("ibl_irradiance", "", "", "ibl_irradiance.comp", {}, info, rendering::scene_render::render_queue::opaque);
    }();

    return shd;
}

const rendering::scene_render::shader* prefilter_shader()
{
    static const rendering::scene_render::shader* shd = [] {
        rendering::compute_pipeline_create_info info;
        info.set_layouts        = { bake_set_layout() };
        info.push_constant_size = sizeof(prefilter_push);

        return rendering::scene_render::res::shaders::build("ibl_prefilter", "", "", "ibl_prefilter.comp", {}, info, rendering::scene_render::render_queue::opaque);
    }();

    return shd;
}

uint32_t get_mip_levels(uint32_t size)
{
    uint32_t levels = 1;

    for (; size > 1; size /= 2) {
        levels++;
    }

    return levels;
}

uint32_t get_groups(uint32_t size)
{
    return (size + BAKE_GROUP - 1) / BAKE_GROUP;
}

void write_bake_set(VkDescriptorSet set, const rendering::image& source, VkImageView target)
{
    rendering::vk::descriptors::write_image(
        set,
        0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        source.view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        rendering::scene_render::environment::get_cube_sampler()
    );

    rendering::vk::descriptors::write_image(set, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, target, VK_IMAGE_LAYOUT_GENERAL);
}

void transition_bake_target(VkCommandBuffer cmd, const rendering::image& target, VkImageLayout old_layout, VkImageLayout new_layout)
{
    const bool to_general = new_layout == VK_IMAGE_LAYOUT_GENERAL;

    rendering::vk::commands::transition_image(
        cmd,
        target.handle,
        old_layout,
        new_layout,
        to_general ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        to_general ? 0 : VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        to_general ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        to_general ? VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        target.mip_levels,
        target.array_layers
    );
}

void bake(const rendering::image& sky, const rendering::buffer& staging, const rendering::image& irradiance, const rendering::image& prefiltered)
{
    const uint32_t sets_count = 1 + PREFILTERED_MIPS;

    const VkDescriptorPool pool = rendering::vk::descriptors::create_pool(
        sets_count,
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sets_count },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          sets_count }
        }
    );

    const VkImageView irradiance_view = rendering::vk::resources::create_image_view(irradiance, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1);

    const VkDescriptorSet irradiance_set = rendering::vk::descriptors::allocate_set(pool, bake_set_layout());

    write_bake_set(irradiance_set, sky, irradiance_view);

    std::vector<VkImageView> prefiltered_views;
    std::vector<VkDescriptorSet> prefiltered_sets;

    for (uint32_t mip = 0; mip < PREFILTERED_MIPS; mip++) {
        prefiltered_views.push_back(rendering::vk::resources::create_image_view(prefiltered, VK_IMAGE_VIEW_TYPE_2D_ARRAY, mip, 1));
        prefiltered_sets.push_back(rendering::vk::descriptors::allocate_set(pool, bake_set_layout()));

        write_bake_set(prefiltered_sets.back(), sky, prefiltered_views.back());
    }

    const rendering::scene_render::shader* irradiance_target = irradiance_shader();
    const rendering::scene_render::shader* prefilter_target  = prefilter_shader();

    rendering::vk::commands::immediate_submit([&](VkCommandBuffer cmd) {
        const vkb::DispatchTable& disp = rendering::vk::device::get_disp();

        rendering::vk::resources::record_image_upload(cmd, sky, staging);

        rendering::vk::commands::memory_barrier(
            cmd,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
        );

        transition_bake_target(cmd, irradiance, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        transition_bake_target(cmd, prefiltered, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, irradiance_target->compute.layout, 0, 1, &irradiance_set, 0, nullptr);

        rendering::vk::commands::dispatch(
            cmd,
            irradiance_target->compute,
            nullptr,
            0,
            get_groups(IRRADIANCE_SIZE),
            get_groups(IRRADIANCE_SIZE),
            FACES_COUNT
        );

        for (uint32_t mip = 0; mip < PREFILTERED_MIPS; mip++) {
            const uint32_t mip_size = std::max(PREFILTERED_SIZE >> mip, 1u);

            const prefilter_push push = {
                static_cast<float>(mip) / static_cast<float>(PREFILTERED_MIPS - 1),
                static_cast<float>(sky.extent.width)
            };

            disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, prefilter_target->compute.layout, 0, 1, &prefiltered_sets[mip], 0, nullptr);

            rendering::vk::commands::dispatch(
                cmd,
                prefilter_target->compute,
                &push,
                sizeof(push),
                get_groups(mip_size),
                get_groups(mip_size),
                FACES_COUNT
            );
        }

        transition_bake_target(cmd, irradiance, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        transition_bake_target(cmd, prefiltered, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    for (const VkImageView view : prefiltered_views) {
        rendering::vk::resources::destroy_image_view(view);
    }

    rendering::vk::resources::destroy_image_view(irradiance_view);

    rendering::vk::descriptors::destroy_pool(pool);
}

void write_environment_image(VkDescriptorSet set, uint32_t binding, const rendering::image& source)
{
    rendering::vk::descriptors::write_image(
        set,
        binding,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        source.view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        rendering::scene_render::environment::get_cube_sampler()
    );
}

}

cmp_cubemap::cmp_cubemap(const std::string& dir, float intensity) : m_intensity(intensity)
{
    set_flag(scene::vol::component_flags::modeled);
    set_flag(scene::vol::component_flags::environment);

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
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        get_mip_levels(size),
        FACES_COUNT,
        true
    );

    m_irradiance = rendering::vk::resources::create_image(
        { IRRADIANCE_SIZE, IRRADIANCE_SIZE, 1 },
        IBL_FORMAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1,
        FACES_COUNT,
        true
    );

    m_prefiltered = rendering::vk::resources::create_image(
        { PREFILTERED_SIZE, PREFILTERED_SIZE, 1 },
        IBL_FORMAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        PREFILTERED_MIPS,
        FACES_COUNT,
        true
    );

    rendering::buffer staging = rendering::vk::resources::create_buffer(
        pixels.size(),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    rendering::vk::resources::upload_buffer(staging, pixels.data(), pixels.size());

    bake(m_image, staging, m_irradiance, m_prefiltered);

    rendering::vk::resources::destroy_buffer(staging);

    m_pool = rendering::vk::descriptors::create_pool(1, { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 } });
    m_set  = rendering::vk::descriptors::allocate_set(m_pool, rendering::scene_render::environment::get_set_layout());

    write_environment_image(m_set, rendering::scene_render::environment::SKY_BINDING, m_image);
    write_environment_image(m_set, rendering::scene_render::environment::IRRADIANCE_BINDING, m_irradiance);
    write_environment_image(m_set, rendering::scene_render::environment::PREFILTERED_BINDING, m_prefiltered);

    rendering::scene_render::environment::write_brdf_lut(m_set);

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

    rendering::vk::resources::destroy_image(m_prefiltered);
    rendering::vk::resources::destroy_image(m_irradiance);
    rendering::vk::resources::destroy_image(m_image);
}

rendering::scene_render::gpu_model cmp_cubemap::get_model()
{
    rendering::scene_render::gpu_model result;

    result.parts.push_back({ m_mesh, m_material, glm::mat4(1.0f) });

    return result;
}

rendering::scene_render::gpu_environment cmp_cubemap::get_environment()
{
    rendering::scene_render::gpu_environment result;

    result.set       = m_set;
    result.intensity = m_intensity;

    return result;
}

cubemap::cubemap(const std::string& dir, float intensity)
{
    m_scene_volume().components.push_back(std::make_unique<cmp_cubemap>(dir, intensity));
}

}
