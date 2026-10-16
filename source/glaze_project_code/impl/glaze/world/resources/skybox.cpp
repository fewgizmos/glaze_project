#include "glp/glaze/world/resources/skybox.h"

#include "glp/glaze/rendering/scene_render/environment.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"
#include "glp/glaze/rendering/queue_deletor.h"

#include "glp/glaze/utilities/core/assert.h"

#include <stb_image.h>

#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

constexpr float PANORAMA_MAX_VALUE = 60000.0f;

constexpr float SUN_THRESHOLD_FRACTION = 0.5f;
constexpr float SUN_REPLACE_LUMINANCE  = 5.0f;

const glm::vec3 LUMINANCE_WEIGHTS = glm::vec3(0.2126f, 0.7152f, 0.0722f);

struct sun_data
{
    bool found = false;

    glm::vec3 direction  = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 irradiance = glm::vec3(0.0f);
};

struct prefilter_push
{
    float roughness;
    float source_resolution;
};

struct sky_source
{
    rendering::buffer staging;
    rendering::image panorama;

    VkImageView sky_view = VK_NULL_HANDLE;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set   = VK_NULL_HANDLE;
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

const rendering::scene_render::shader* panorama_shader()
{
    static const rendering::scene_render::shader* shd = [] {
        rendering::compute_pipeline_create_info info;
        info.set_layouts = { bake_set_layout() };

        return rendering::scene_render::res::shaders::build("ibl_panorama", "", "", "ibl_panorama.comp", {}, info, rendering::scene_render::render_queue::opaque);
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

float pixel_luminance(const float* pixel)
{
    return glm::dot(glm::vec3(pixel[0], pixel[1], pixel[2]), LUMINANCE_WEIGHTS);
}

std::vector<size_t> find_sun_cluster(const std::vector<float>& luminance, int width, int height, float threshold)
{
    std::vector<bool> visited(luminance.size(), false);

    std::vector<size_t> best;
    std::vector<size_t> current;
    std::vector<size_t> stack;

    for (size_t start = 0; start < luminance.size(); start++) {
        if (visited[start] || luminance[start] < threshold) {
            continue;
        }

        current.clear();
        stack.assign(1, start);

        visited[start] = true;

        while (!stack.empty()) {
            const size_t index = stack.back();

            stack.pop_back();
            current.push_back(index);

            const int x = static_cast<int>(index % width);
            const int y = static_cast<int>(index / width);

            const int neighbours[4][2] = {
                { (x + 1) % width, y },
                { (x + width - 1) % width, y },
                { x, y + 1 },
                { x, y - 1 }
            };

            for (const auto& neighbour : neighbours) {
                if (neighbour[1] < 0 || neighbour[1] >= height) {
                    continue;
                }

                const size_t next = static_cast<size_t>(neighbour[1]) * width + neighbour[0];

                if (visited[next] || luminance[next] < threshold) {
                    continue;
                }

                visited[next] = true;

                stack.push_back(next);
            }
        }

        if (current.size() > best.size()) {
            best.swap(current);
        }
    }

    return best;
}

sun_data extract_sun(float* pixels, int width, int height)
{
    const size_t pixels_count = static_cast<size_t>(width) * height;

    std::vector<float> luminance(pixels_count);

    float max_luminance = 0.0f;

    for (size_t i = 0; i < pixels_count; i++) {
        luminance[i] = pixel_luminance(pixels + i * PIXEL_SIZE);

        max_luminance = std::max(max_luminance, luminance[i]);
    }

    sun_data result;

    if (max_luminance <= 0.0f) {
        return result;
    }

    const std::vector<size_t> cluster = find_sun_cluster(luminance, width, height, max_luminance * SUN_THRESHOLD_FRACTION);

    const float pixel_phi   = 2.0f * glm::pi<float>() / static_cast<float>(width);
    const float pixel_theta = glm::pi<float>() / static_cast<float>(height);

    glm::vec3 weighted_direction(0.0f);

    for (const size_t index : cluster) {
        const float u = (static_cast<float>(index % width) + 0.5f) / static_cast<float>(width);
        const float v = (static_cast<float>(index / width) + 0.5f) / static_cast<float>(height);

        const float phi   = (u - 0.5f) * 2.0f * glm::pi<float>();
        const float theta = v * glm::pi<float>();

        const glm::vec3 direction(glm::sin(theta) * glm::cos(phi), glm::cos(theta), glm::sin(theta) * glm::sin(phi));

        const float solid_angle = pixel_phi * pixel_theta * glm::sin(theta);

        float* pixel = pixels + index * PIXEL_SIZE;

        result.irradiance  += glm::vec3(pixel[0], pixel[1], pixel[2]) * solid_angle;
        weighted_direction += direction * luminance[index] * solid_angle;

        const float scale = SUN_REPLACE_LUMINANCE / std::max(luminance[index], SUN_REPLACE_LUMINANCE);

        pixel[0] *= scale;
        pixel[1] *= scale;
        pixel[2] *= scale;
    }

    if (glm::length(weighted_direction) <= 0.0f) {
        return result;
    }

    result.found     = true;
    result.direction = glm::normalize(weighted_direction);

    return result;
}

rendering::image load_panorama(const std::string& path, sky_source& source, sun_data& sun)
{
    int width    = 0;
    int height   = 0;
    int channels = 0;

    float* pixels = stbi_loadf(path.c_str(), &width, &height, &channels, PIXEL_SIZE);

    GLAZE_ASSERT(pixels == nullptr, "failed to load panorama: " + path);

    sun = extract_sun(pixels, width, height);

    const size_t values_count = static_cast<size_t>(width) * height * PIXEL_SIZE;
    const size_t bytes_count  = values_count * sizeof(float);

    for (size_t i = 0; i < values_count; i++) {
        pixels[i] = std::clamp(pixels[i], 0.0f, PANORAMA_MAX_VALUE);
    }

    source.staging = rendering::vk::resources::create_buffer(
        bytes_count,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    rendering::vk::resources::upload_buffer(source.staging, pixels, bytes_count);

    stbi_image_free(pixels);

    source.panorama = rendering::vk::resources::create_image(
        { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 },
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    const uint32_t size = std::max(static_cast<uint32_t>(height) / 2, 1u);

    rendering::image sky = rendering::vk::resources::create_image(
        { size, size, 1 },
        IBL_FORMAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        get_mip_levels(size),
        FACES_COUNT,
        true
    );

    rendering::sampler_create_info sampler_info;
    sampler_info.mip_mode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.wrap_u     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.wrap_v     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_w     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropy = false;

    source.sky_view = rendering::vk::resources::create_image_view(sky, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1);

    source.pool = rendering::vk::descriptors::create_pool(
        1,
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1 }
        }
    );

    source.set = rendering::vk::descriptors::allocate_set(source.pool, bake_set_layout());

    rendering::vk::descriptors::write_image(
        source.set,
        0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        source.panorama.view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        rendering::vk::samplers::get(rendering::vk::samplers::make(sampler_info))
    );

    rendering::vk::descriptors::write_image(source.set, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, source.sky_view, VK_IMAGE_LAYOUT_GENERAL);

    return sky;
}

void destroy_sky_source(sky_source& source)
{
    rendering::vk::descriptors::destroy_pool(source.pool);

    rendering::vk::resources::destroy_image_view(source.sky_view);
    rendering::vk::resources::destroy_image(source.panorama);
    rendering::vk::resources::destroy_buffer(source.staging);

    source = {};
}

void record_sky(VkCommandBuffer cmd, const rendering::image& sky, const sky_source& source)
{
    const vkb::DispatchTable& disp = rendering::vk::device::get_disp();

    rendering::vk::resources::record_image_upload(cmd, source.panorama, source.staging);

    rendering::vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );

    transition_bake_target(cmd, sky, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    const rendering::scene_render::shader* panorama_target = panorama_shader();

    disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, panorama_target->compute.layout, 0, 1, &source.set, 0, nullptr);

    rendering::vk::commands::dispatch(
        cmd,
        panorama_target->compute,
        nullptr,
        0,
        get_groups(sky.extent.width),
        get_groups(sky.extent.height),
        FACES_COUNT
    );

    rendering::vk::commands::transition_image(
        cmd,
        sky.handle,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        sky.mip_levels,
        sky.array_layers
    );

    rendering::vk::resources::record_mip_generation(cmd, sky);
}

void bake(const rendering::image& sky, const sky_source& source, const rendering::image& irradiance, const rendering::image& prefiltered)
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

        record_sky(cmd, sky, source);

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

cmp_cubemap::cmp_cubemap(const std::string& path, float intensity, float sun_intensity) : m_intensity(intensity)
{
    set_flag(scene::vol::component_flags::modeled);
    set_flag(scene::vol::component_flags::environment);

    sky_source source;
    sun_data sun;

    m_image = load_panorama(path, source, sun);

    if (sun.found) {
        const float peak = std::max(sun.irradiance.r, std::max(sun.irradiance.g, sun.irradiance.b));

        const glm::vec3 forward = -sun.direction;

        m_has_sun = peak > 0.0f;

        m_sun.type      = scene::vol::component::light_type::directional;
        m_sun.color     = peak > 0.0f ? sun.irradiance / peak : glm::vec3(1.0f);
        m_sun.intensity = sun_intensity;

        m_sun_rot = glm::vec3(
            glm::degrees(glm::asin(glm::clamp(forward.y, -1.0f, 1.0f))),
            glm::degrees(std::atan2(-forward.x, -forward.z)),
            0.0f
        );
    }

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

    bake(m_image, source, m_irradiance, m_prefiltered);

    destroy_sky_source(source);

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

skybox::skybox(const std::string& path, float intensity, float sun_intensity)
{
    std::unique_ptr<cmp_cubemap> sky = std::make_unique<cmp_cubemap>(path, intensity, sun_intensity);

    if (sky->has_sun()) {
        std::unique_ptr<scene::vol::component::light> sun = std::make_unique<scene::vol::component::light>(sky->get_sun());

        sun->set_rot(sky->get_sun_rot());

        m_scene_volume().components.push_back(std::move(sun));
    }

    m_scene_volume().components.push_back(std::move(sky));
}

}
