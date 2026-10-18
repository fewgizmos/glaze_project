#include "glp/glaze/rendering/scene_render/atmosphere.h"

#include "glp/glaze/rendering/scene_render/clouds.h"
#include "glp/glaze/rendering/scene_render/environment.h"
#include "glp/glaze/rendering/scene_render/frame_pipeline.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/gpu_atmosphere.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_camera.h"
#include "glp/glaze/rendering/scene_render/resources/gpu_model.h"
#include "glp/glaze/rendering/scene_render/resources/materials.h"
#include "glp/glaze/rendering/scene_render/resources/meshes.h"
#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/resources.h"
#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"

#include "glp/glaze/utilities/core/assert.h"
#include "glp/glaze/utilities/core/timer.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace glaze::rendering::scene_render::atmosphere
{

namespace
{

struct params_data
{
    glm::mat4 inv_view_proj;

    glm::vec3 camera_position;
    float bottom_radius;

    glm::vec3 sun_direction;
    float top_radius;

    glm::vec3 rayleigh_scattering;
    float rayleigh_exp_scale;

    glm::vec3 mie_scattering;
    float mie_exp_scale;

    glm::vec3 mie_extinction;
    float mie_phase_g;

    glm::vec3 absorption_extinction;
    float absorption_center;

    glm::vec3 ground_albedo;
    float absorption_width;

    float sun_illuminance;
    float sun_angular_radius;
    float aerial_max_distance;
    float padding0;

    glm::mat4 previous_view_proj;

    glm::vec3 camera_world_position;
    uint32_t frame_index;

    float cloud_bottom;
    float cloud_top;
    float cloud_coverage;
    float cloud_density;

    float cloud_type;
    uint32_t clouds_enabled;
    float cloud_luminance_scale;
    float time;

    glm::vec3 moon_direction;
    float moon_illuminance;

    glm::vec3 moon_color;
    float moon_angular_radius;

    glm::mat4 celestial_rotation;

    float night_sky_luminance;
    float star_intensity;
    float star_twinkle;
    float night_ambient_scale;

    float cloud_multi_scattering;
    float cloud_powder;
    float moon_flare_size;
    float moon_flare_falloff;

    glm::vec3 moon_flare_tint;
    float moon_flare_multiplier;

    glm::vec3 sun_tint;
    float twilight_desaturation;

    glm::vec3 fog_sun_illuminance;
    float fog_density;

    glm::vec3 fog_moon_illuminance;
    float fog_height_falloff;

    glm::vec3 fog_color;
    float fog_base_height;

    float fog_anisotropy;
    float fog_max_distance;
    float padding1;
    float padding2;
};

struct compute_push
{
    VkDeviceAddress params;
};

constexpr VkExtent3D TRANSMITTANCE_SIZE      = { 256, 64, 1 };
constexpr VkExtent3D MULTI_SCATTERING_SIZE   = { 32, 32, 1 };
constexpr VkExtent3D SKY_VIEW_SIZE           = { 192, 108, 1 };
constexpr VkExtent3D AERIAL_PERSPECTIVE_SIZE = { 32, 32, 32 };

constexpr uint32_t SKY_VIEW_LAYERS = 2;

constexpr uint32_t SKY_CUBE_SIZE = 64;

constexpr uint32_t GROUP_SIZE        = 8;
constexpr uint32_t VOLUME_GROUP_SIZE = 4;

constexpr VkFormat LUT_FORMAT         = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat PRECISE_LUT_FORMAT = VK_FORMAT_R32G32B32A32_SFLOAT;

constexpr float METERS_TO_KILOMETERS = 0.001f;
constexpr float PLANET_SURFACE_OFFSET = 0.001f;

constexpr float IBL_UPDATE_COS = 0.99999962f;

constexpr float IBL_MEDIUM_TOLERANCE = 0.002f;

constexpr float IBL_MIN_INTERVAL = 0.2f;

constexpr float MIN_CLOUD_REFERENCE = 0.000001f;
constexpr float NIGHT_SUN_HEIGHT    = -0.2f;
constexpr float DAY_SUN_HEIGHT      = -0.1f;

constexpr float TWILIGHT_START_HEIGHT = 0.05f;
constexpr float TWILIGHT_PEAK_HEIGHT  = -0.1f;
constexpr float TWILIGHT_END_HEIGHT   = -0.25f;

constexpr float NIGHT_AMBIENT_SUN_HEIGHT = -0.1f;
constexpr float DAY_AMBIENT_SUN_HEIGHT   = 0.1f;

constexpr uint32_t SETS_COUNT = 6;

VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;

VkDescriptorPool m_pool = VK_NULL_HANDLE;

VkDescriptorSet m_transmittance_set = VK_NULL_HANDLE;
VkDescriptorSet m_multi_scattering_set = VK_NULL_HANDLE;
VkDescriptorSet m_sky_view_set = VK_NULL_HANDLE;
VkDescriptorSet m_aerial_set = VK_NULL_HANDLE;
VkDescriptorSet m_sky_cube_set = VK_NULL_HANDLE;
VkDescriptorSet m_draw_set = VK_NULL_HANDLE;

VkDescriptorPool m_environment_pool = VK_NULL_HANDLE;
VkDescriptorSet m_environment_set = VK_NULL_HANDLE;

VkDescriptorPool m_bake_pool = VK_NULL_HANDLE;
VkDescriptorSet m_prefilter_sets[environment::PREFILTERED_MIPS] = {};
VkDescriptorSet m_irradiance_set = VK_NULL_HANDLE;

image m_transmittance;
image m_multi_scattering;
image m_sky_view;
image m_aerial_perspective;
image m_moon_aerial_perspective;
image m_sky_cube;
image m_prefiltered;

VkImageView m_sky_cube_storage_view = VK_NULL_HANDLE;
VkImageView m_prefiltered_views[environment::PREFILTERED_MIPS] = {};

buffer m_sh_buffer;
buffer m_params_buffers[MAX_FRAMES_IN_FLIGHT];

VkSampler m_sampler = VK_NULL_HANDLE;

const shader* m_transmittance_shader = nullptr;
const shader* m_multi_scattering_shader = nullptr;
const shader* m_sky_view_shader = nullptr;
const shader* m_aerial_shader = nullptr;
const shader* m_sky_cube_shader = nullptr;
const shader* m_sky_shader = nullptr;

const mesh* m_sky_mesh = nullptr;
const material* m_sky_material = nullptr;

gpu_atmosphere m_lut_atmosphere;
gpu_atmosphere m_ibl_atmosphere;

bool m_luts_dirty = true;
bool m_ibl_dirty  = true;

bool m_ibl_baked      = false;
float m_ibl_bake_time = 0.0f;

bool m_active = false;

glm::mat4 m_previous_view_proj = glm::mat4(1.0f);
bool m_has_previous_view_proj  = false;

uint32_t m_frame_index = 0;

VkImageView m_written_clouds_view = VK_NULL_HANDLE;

uint32_t get_mip_levels(uint32_t size)
{
    uint32_t levels = 1;

    for (; size > 1; size /= 2) {
        levels++;
    }

    return levels;
}

uint32_t get_groups(uint32_t size, uint32_t group)
{
    return (size + group - 1) / group;
}

bool same_medium(const gpu_atmosphere& left, const gpu_atmosphere& right)
{
    return left.bottom_radius == right.bottom_radius
        && left.top_radius == right.top_radius
        && left.rayleigh_scattering == right.rayleigh_scattering
        && left.rayleigh_scale_height == right.rayleigh_scale_height
        && left.mie_scattering == right.mie_scattering
        && left.mie_absorption == right.mie_absorption
        && left.mie_scale_height == right.mie_scale_height
        && left.mie_anisotropy == right.mie_anisotropy
        && left.ozone_absorption == right.ozone_absorption
        && left.ozone_center == right.ozone_center
        && left.ozone_width == right.ozone_width
        && left.ground_albedo == right.ground_albedo;
}

float night_factor(const gpu_atmosphere& atmosphere)
{
    return 1.0f - glm::smoothstep(NIGHT_AMBIENT_SUN_HEIGHT, DAY_AMBIENT_SUN_HEIGHT, glm::normalize(atmosphere.sun_direction).y);
}

float night_ambient_scale(const gpu_atmosphere& atmosphere)
{
    return glm::mix(1.0f, atmosphere.night_ambient_scale, night_factor(atmosphere));
}

float twilight_desaturation(const gpu_atmosphere& atmosphere)
{
    const float height = glm::normalize(atmosphere.sun_direction).y;

    const float twilight = glm::smoothstep(TWILIGHT_END_HEIGHT, TWILIGHT_PEAK_HEIGHT, height) * (1.0f - glm::smoothstep(TWILIGHT_PEAK_HEIGHT, TWILIGHT_START_HEIGHT, height));

    return atmosphere.twilight_desaturation * twilight;
}

bool similar(glm::vec3 left, glm::vec3 right)
{
    return glm::length(left - right) <= IBL_MEDIUM_TOLERANCE * glm::length(right);
}

bool similar_medium(const gpu_atmosphere& left, const gpu_atmosphere& right)
{
    return similar(left.mie_scattering, right.mie_scattering)
        && similar(left.mie_absorption, right.mie_absorption)
        && std::abs(left.mie_scale_height - right.mie_scale_height) <= IBL_MEDIUM_TOLERANCE * right.mie_scale_height
        && left.bottom_radius == right.bottom_radius
        && left.top_radius == right.top_radius
        && left.rayleigh_scattering == right.rayleigh_scattering
        && left.rayleigh_scale_height == right.rayleigh_scale_height
        && left.mie_anisotropy == right.mie_anisotropy
        && left.ozone_absorption == right.ozone_absorption
        && left.ozone_center == right.ozone_center
        && left.ozone_width == right.ozone_width
        && left.ground_albedo == right.ground_albedo;
}

image create_lut(VkExtent3D extent)
{
    return vk::resources::create_image(extent, LUT_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
}

const shader* build_compute(const char* name, const char* file)
{
    compute_pipeline_create_info info;
    info.set_layouts        = { m_set_layout };
    info.push_constant_size = sizeof(compute_push);

    return res::shaders::build(name, "", "", file, {}, info, render_queue::opaque);
}

void write_sampled(VkDescriptorSet set, uint32_t binding, const image& source)
{
    vk::descriptors::write_image(set, binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, source.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_sampler);
}

void write_storage(VkDescriptorSet set, VkImageView target)
{
    vk::descriptors::write_image(set, 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, target, VK_IMAGE_LAYOUT_GENERAL);
}

void create_layout_and_sets()
{
    const VkShaderStageFlags stages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    m_set_layout = vk::descriptors::create_set_layout({
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, stages, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, stages, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, stages, nullptr },
        { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, stages, nullptr },
        { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, stages, nullptr },
        { 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, stages, nullptr }
    });

    m_pool = vk::descriptors::create_pool(
        SETS_COUNT,
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SETS_COUNT * 4 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          SETS_COUNT * 2 }
        }
    );

    m_transmittance_set    = vk::descriptors::allocate_set(m_pool, m_set_layout);
    m_multi_scattering_set = vk::descriptors::allocate_set(m_pool, m_set_layout);
    m_sky_view_set         = vk::descriptors::allocate_set(m_pool, m_set_layout);
    m_aerial_set           = vk::descriptors::allocate_set(m_pool, m_set_layout);
    m_sky_cube_set         = vk::descriptors::allocate_set(m_pool, m_set_layout);
    m_draw_set             = vk::descriptors::allocate_set(m_pool, m_set_layout);

    write_storage(m_transmittance_set, m_transmittance.view);

    write_sampled(m_multi_scattering_set, 0, m_transmittance);
    write_storage(m_multi_scattering_set, m_multi_scattering.view);

    write_sampled(m_sky_view_set, 0, m_transmittance);
    write_sampled(m_sky_view_set, 1, m_multi_scattering);
    write_storage(m_sky_view_set, m_sky_view.view);

    write_sampled(m_aerial_set, 0, m_transmittance);
    write_sampled(m_aerial_set, 1, m_multi_scattering);
    write_storage(m_aerial_set, m_aerial_perspective.view);

    vk::descriptors::write_image(m_aerial_set, 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_moon_aerial_perspective.view, VK_IMAGE_LAYOUT_GENERAL);

    write_sampled(m_sky_cube_set, 0, m_transmittance);
    write_sampled(m_sky_cube_set, 2, m_sky_view);
    write_storage(m_sky_cube_set, m_sky_cube_storage_view);

    write_sampled(m_draw_set, 0, m_transmittance);
    write_sampled(m_draw_set, 2, m_sky_view);

    m_environment_pool = vk::descriptors::create_pool(1, { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, environment::SET_SAMPLERS_COUNT } });
    m_environment_set  = vk::descriptors::allocate_set(m_environment_pool, environment::get_set_layout());

    vk::descriptors::write_image(m_environment_set, environment::SKY_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_sky_cube.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, environment::get_cube_sampler());
    vk::descriptors::write_image(m_environment_set, environment::PREFILTERED_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_prefiltered.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, environment::get_cube_sampler());
    vk::descriptors::write_image(m_environment_set, environment::AERIAL_PERSPECTIVE_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_aerial_perspective.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_sampler);
    vk::descriptors::write_image(m_environment_set, environment::MOON_AERIAL_PERSPECTIVE_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_moon_aerial_perspective.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_sampler);

    environment::write_brdf_lut(m_environment_set);

    const uint32_t bake_sets = environment::PREFILTERED_MIPS + 1;

    m_bake_pool = vk::descriptors::create_pool(
        bake_sets,
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, bake_sets },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          bake_sets }
        }
    );

    for (uint32_t mip = 0; mip < environment::PREFILTERED_MIPS; mip++) {
        m_prefilter_sets[mip] = vk::descriptors::allocate_set(m_bake_pool, environment::get_bake_set_layout());

        environment::write_bake_set(m_prefilter_sets[mip], m_sky_cube, m_prefiltered_views[mip]);
    }

    m_irradiance_set = vk::descriptors::allocate_set(m_bake_pool, environment::get_bake_set_layout());

    environment::write_bake_set(m_irradiance_set, m_sky_cube, VK_NULL_HANDLE);
}

void create_images()
{
    m_transmittance    = create_lut(TRANSMITTANCE_SIZE);
    m_multi_scattering = create_lut(MULTI_SCATTERING_SIZE);
    m_sky_view = vk::resources::create_image(
        SKY_VIEW_SIZE,
        PRECISE_LUT_FORMAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1,
        SKY_VIEW_LAYERS
    );

    m_aerial_perspective      = vk::resources::create_volume_image(AERIAL_PERSPECTIVE_SIZE, PRECISE_LUT_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    m_moon_aerial_perspective = vk::resources::create_volume_image(AERIAL_PERSPECTIVE_SIZE, PRECISE_LUT_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    m_sky_cube = vk::resources::create_image(
        { SKY_CUBE_SIZE, SKY_CUBE_SIZE, 1 },
        environment::IBL_FORMAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        get_mip_levels(SKY_CUBE_SIZE),
        environment::CUBE_FACES,
        true
    );

    m_prefiltered = vk::resources::create_image(
        { environment::PREFILTERED_SIZE, environment::PREFILTERED_SIZE, 1 },
        environment::IBL_FORMAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        environment::PREFILTERED_MIPS,
        environment::CUBE_FACES,
        true
    );

    m_sky_cube_storage_view = vk::resources::create_image_view(m_sky_cube, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1);

    for (uint32_t mip = 0; mip < environment::PREFILTERED_MIPS; mip++) {
        m_prefiltered_views[mip] = vk::resources::create_image_view(m_prefiltered, VK_IMAGE_VIEW_TYPE_2D_ARRAY, mip, 1);
    }

    m_sh_buffer = vk::resources::create_buffer(
        sizeof(glm::vec4) * ENVIRONMENT_SH_COUNT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    );

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_params_buffers[i] = vk::resources::create_buffer(
            sizeof(params_data),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        );
    }
}

void create_sampler()
{
    sampler_create_info sampler_info;
    sampler_info.mip_mode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.wrap_u     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_v     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_w     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropy = false;

    m_sampler = vk::samplers::get(vk::samplers::make(sampler_info));
}

void create_shaders()
{
    m_transmittance_shader    = build_compute("atmosphere_transmittance", "sys/atmosphere_transmittance.comp");
    m_multi_scattering_shader = build_compute("atmosphere_multi_scattering", "sys/atmosphere_multi_scattering.comp");
    m_sky_view_shader         = build_compute("atmosphere_sky_view", "sys/atmosphere_sky_view.comp");
    m_aerial_shader           = build_compute("atmosphere_aerial_perspective", "sys/atmosphere_aerial_perspective.comp");
    m_sky_cube_shader         = build_compute("atmosphere_sky_cube", "sys/atmosphere_sky_cube.comp");

    graphics_pipeline_create_info info;
    info.color_formats        = { HDR_FORMAT };
    info.depth_format         = DEPTH_FORMAT;
    info.cull_mode            = VK_CULL_MODE_NONE;
    info.depth_write          = false;
    info.set_layouts          = { it::get_scene_set_layout(), m_set_layout };
    info.push_constant_size   = sizeof(VkDeviceAddress);
    info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    m_sky_shader = res::shaders::build("atmosphere_sky", "sys/atmosphere_sky.vert", "sys/atmosphere_sky.frag", "", info, {}, render_queue::skybox);
}

void create_sky_model()
{
    const std::vector<vertex> vertices(3, vertex{});
    const std::vector<uint32_t> indices = { 0, 1, 2 };

    m_sky_mesh = res::meshes::build(vertices, indices);

    material_create_info create_info;
    create_info.target_shader = m_sky_shader;
    create_info.extra_set     = m_draw_set;

    m_sky_material = res::materials::build(create_info);
}

params_data build_params(const gpu_atmosphere& atmosphere)
{
    const gpu_camera& camera = frame_pipe::get_camera();

    params_data data;

    data.inv_view_proj = camera.inv_view_proj;

    data.camera_position = glm::vec3(
        camera.position.x * METERS_TO_KILOMETERS,
        atmosphere.bottom_radius + PLANET_SURFACE_OFFSET + std::max(camera.position.y * METERS_TO_KILOMETERS, 0.0f),
        camera.position.z * METERS_TO_KILOMETERS
    );

    data.bottom_radius = atmosphere.bottom_radius;
    data.top_radius    = atmosphere.top_radius;

    data.sun_direction = glm::normalize(atmosphere.sun_direction);

    data.rayleigh_scattering = atmosphere.rayleigh_scattering;
    data.rayleigh_exp_scale  = -1.0f / atmosphere.rayleigh_scale_height;

    data.mie_scattering = atmosphere.mie_scattering;
    data.mie_exp_scale  = -1.0f / atmosphere.mie_scale_height;
    data.mie_extinction = atmosphere.mie_scattering + atmosphere.mie_absorption;
    data.mie_phase_g    = atmosphere.mie_anisotropy;

    data.absorption_extinction = atmosphere.ozone_absorption;
    data.absorption_center     = atmosphere.ozone_center;
    data.absorption_width      = atmosphere.ozone_width;

    data.ground_albedo = atmosphere.ground_albedo;

    data.sun_illuminance     = atmosphere.sun_illuminance;
    data.sun_angular_radius  = glm::radians(atmosphere.sun_angular_radius);
    data.aerial_max_distance = atmosphere.aerial_max_distance;
    data.padding0            = 0.0f;

    data.previous_view_proj = m_has_previous_view_proj ? m_previous_view_proj : camera.view_proj;

    data.camera_world_position = camera.position;
    data.frame_index           = m_frame_index;

    data.cloud_bottom   = atmosphere.bottom_radius + atmosphere.cloud_bottom;
    data.cloud_top      = atmosphere.bottom_radius + atmosphere.cloud_bottom + atmosphere.cloud_thickness;
    data.cloud_coverage = atmosphere.cloud_coverage;
    data.cloud_density  = atmosphere.cloud_density;

    const float moon_reference = std::max(atmosphere.moon_illuminance * std::max({ atmosphere.moon_color.r, atmosphere.moon_color.g, atmosphere.moon_color.b }), MIN_CLOUD_REFERENCE);
    const float day_factor     = glm::smoothstep(NIGHT_SUN_HEIGHT, DAY_SUN_HEIGHT, data.sun_direction.y);

    data.cloud_type            = atmosphere.cloud_type;
    data.clouds_enabled        = atmosphere.clouds_enabled ? 1 : 0;
    data.cloud_luminance_scale = 1.0f / glm::mix(moon_reference, atmosphere.sun_illuminance, day_factor);
    data.time                  = utilities::core::timer::timestamp;

    data.moon_direction   = glm::normalize(atmosphere.moon_direction);
    data.moon_illuminance = atmosphere.moon_illuminance;

    data.moon_color          = atmosphere.moon_color;
    data.moon_angular_radius = glm::radians(atmosphere.moon_angular_radius);

    data.celestial_rotation = atmosphere.celestial_rotation;

    data.night_sky_luminance = atmosphere.night_sky_luminance;
    data.star_intensity      = atmosphere.star_intensity;
    data.star_twinkle        = atmosphere.star_twinkle;
    data.night_ambient_scale = night_ambient_scale(atmosphere);

    data.cloud_multi_scattering = atmosphere.cloud_multi_scattering;
    data.cloud_powder           = atmosphere.cloud_powder;
    data.moon_flare_size        = glm::radians(atmosphere.moon_flare_size);
    data.moon_flare_falloff     = atmosphere.moon_flare_falloff;

    data.moon_flare_tint       = atmosphere.moon_flare_tint;
    data.moon_flare_multiplier = atmosphere.moon_flare_multiplier;

    data.sun_tint              = atmosphere.sun_tint;
    data.twilight_desaturation = twilight_desaturation(atmosphere);

    data.fog_sun_illuminance = atmosphere.sun_light;
    data.fog_density         = glm::mix(atmosphere.fog_day_density, atmosphere.fog_night_density, night_factor(atmosphere));

    data.fog_moon_illuminance = atmosphere.moon_light;
    data.fog_height_falloff   = atmosphere.fog_height_falloff;

    data.fog_color       = atmosphere.fog_color;
    data.fog_base_height = atmosphere.fog_base_height;

    data.fog_anisotropy   = atmosphere.fog_anisotropy;
    data.fog_max_distance = atmosphere.fog_max_distance;
    data.padding1         = 0.0f;
    data.padding2         = 0.0f;

    return data;
}

void dispatch_lut(VkCommandBuffer cmd, const shader* target_shader, VkDescriptorSet set, const image& target, uint32_t groups_x, uint32_t groups_y, uint32_t groups_z)
{
    const compute_push push = { get_params_address() };

    environment::transition_bake_target(cmd, target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    vk::device::get_disp().cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, target_shader->compute.layout, 0, 1, &set, 0, nullptr);

    vk::commands::dispatch(cmd, target_shader->compute, &push, sizeof(push), groups_x, groups_y, groups_z);

    environment::transition_bake_target(cmd, target, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void dispatch_environment(VkCommandBuffer cmd)
{
    const compute_push push = { get_params_address() };

    environment::transition_bake_target(cmd, m_sky_cube, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    vk::device::get_disp().cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_sky_cube_shader->compute.layout, 0, 1, &m_sky_cube_set, 0, nullptr);

    vk::commands::dispatch(
        cmd,
        m_sky_cube_shader->compute,
        &push,
        sizeof(push),
        get_groups(SKY_CUBE_SIZE, GROUP_SIZE),
        get_groups(SKY_CUBE_SIZE, GROUP_SIZE),
        environment::CUBE_FACES
    );

    vk::commands::transition_image(
        cmd,
        m_sky_cube.handle,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        m_sky_cube.mip_levels,
        m_sky_cube.array_layers
    );

    vk::resources::record_mip_generation(cmd, m_sky_cube);

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );

    environment::record_prefilter(cmd, m_prefiltered, m_prefilter_sets, static_cast<float>(SKY_CUBE_SIZE));

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    environment::record_irradiance_sh(cmd, m_irradiance_set, m_sh_buffer.address);
}

}

void init_all()
{
    create_images();

    create_sampler();

    create_layout_and_sets();

    create_shaders();

    create_sky_model();
}

void clean_all()
{
    res::materials::erase(m_sky_material);
    res::meshes::erase(m_sky_mesh);

    vk::descriptors::destroy_pool(m_bake_pool);
    vk::descriptors::destroy_pool(m_environment_pool);
    vk::descriptors::destroy_pool(m_pool);
    vk::descriptors::destroy_set_layout(m_set_layout);

    for (uint32_t mip = 0; mip < environment::PREFILTERED_MIPS; mip++) {
        vk::resources::destroy_image_view(m_prefiltered_views[mip]);
    }

    vk::resources::destroy_image_view(m_sky_cube_storage_view);

    vk::resources::destroy_image(m_prefiltered);
    vk::resources::destroy_image(m_sky_cube);
    vk::resources::destroy_image(m_moon_aerial_perspective);
    vk::resources::destroy_image(m_aerial_perspective);
    vk::resources::destroy_image(m_sky_view);
    vk::resources::destroy_image(m_multi_scattering);
    vk::resources::destroy_image(m_transmittance);

    vk::resources::destroy_buffer(m_sh_buffer);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::resources::destroy_buffer(m_params_buffers[i]);
    }

    m_sky_material = nullptr;
    m_sky_mesh     = nullptr;

    m_luts_dirty = true;
    m_ibl_dirty  = true;
    m_active     = false;

    m_ibl_baked = false;

    m_has_previous_view_proj = false;
    m_frame_index            = 0;

    m_written_clouds_view = VK_NULL_HANDLE;
}

void handle_all()
{
    m_active = frame_pipe::has_atmosphere() && frame_pipe::has_camera();

    if (!m_active) {
        return;
    }

    GLAZE_ASSERT(frame_pipe::has_environment(), "skybox and sky_atmosphere can not be used in one scene");

    const gpu_atmosphere& atmosphere = frame_pipe::get_atmosphere();

    if (!same_medium(atmosphere, m_lut_atmosphere)) {
        m_luts_dirty = true;
    }

    if (!similar_medium(atmosphere, m_ibl_atmosphere)
        || glm::dot(glm::normalize(atmosphere.sun_direction), glm::normalize(m_ibl_atmosphere.sun_direction)) < IBL_UPDATE_COS
        || glm::dot(glm::normalize(atmosphere.moon_direction), glm::normalize(m_ibl_atmosphere.moon_direction)) < IBL_UPDATE_COS
        || atmosphere.sun_illuminance != m_ibl_atmosphere.sun_illuminance
        || atmosphere.moon_illuminance != m_ibl_atmosphere.moon_illuminance
        || atmosphere.moon_color != m_ibl_atmosphere.moon_color
        || atmosphere.night_sky_luminance != m_ibl_atmosphere.night_sky_luminance) {
        m_ibl_dirty = true;
    }

    const params_data data = build_params(atmosphere);

    vk::resources::upload_buffer(m_params_buffers[vk::frame::get_current_frame_index()], &data, sizeof(data));

    m_previous_view_proj     = frame_pipe::get_camera().view_proj;
    m_has_previous_view_proj = true;

    m_frame_index++;

    const VkImageView clouds_view = clouds::get_output_image().view;

    if (clouds_view != m_written_clouds_view) {
        write_sampled(m_draw_set, 4, clouds::get_output_image());

        m_written_clouds_view = clouds_view;
    }

    gpu_environment environment_data;
    environment_data.set           = m_environment_set;
    environment_data.intensity     = night_ambient_scale(atmosphere);
    environment_data.irradiance_sh = m_sh_buffer.address;

    frame_pipe::set_environment(environment_data);

    gpu_model sky;

    sky.parts.push_back({ m_sky_mesh, m_sky_material, glm::mat4(1.0f), glm::mat4(1.0f) });

    frame_pipe::submit_model(sky);
}

void dispatch()
{
    if (!m_active) {
        return;
    }

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    if (m_luts_dirty) {
        dispatch_lut(cmd, m_transmittance_shader, m_transmittance_set, m_transmittance, get_groups(TRANSMITTANCE_SIZE.width, GROUP_SIZE), get_groups(TRANSMITTANCE_SIZE.height, GROUP_SIZE), 1);
        dispatch_lut(cmd, m_multi_scattering_shader, m_multi_scattering_set, m_multi_scattering, get_groups(MULTI_SCATTERING_SIZE.width, GROUP_SIZE), get_groups(MULTI_SCATTERING_SIZE.height, GROUP_SIZE), 1);

        m_lut_atmosphere = frame_pipe::get_atmosphere();
    }

    dispatch_lut(cmd, m_sky_view_shader, m_sky_view_set, m_sky_view, get_groups(SKY_VIEW_SIZE.width, GROUP_SIZE), get_groups(SKY_VIEW_SIZE.height, GROUP_SIZE), SKY_VIEW_LAYERS);

    environment::transition_bake_target(cmd, m_moon_aerial_perspective, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    dispatch_lut(
        cmd,
        m_aerial_shader,
        m_aerial_set,
        m_aerial_perspective,
        get_groups(AERIAL_PERSPECTIVE_SIZE.width, VOLUME_GROUP_SIZE),
        get_groups(AERIAL_PERSPECTIVE_SIZE.height, VOLUME_GROUP_SIZE),
        get_groups(AERIAL_PERSPECTIVE_SIZE.depth, VOLUME_GROUP_SIZE)
    );

    environment::transition_bake_target(cmd, m_moon_aerial_perspective, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    const float now = utilities::core::timer::timestamp;

    if (m_ibl_dirty && (!m_ibl_baked || now - m_ibl_bake_time >= IBL_MIN_INTERVAL)) {
        dispatch_environment(cmd);

        m_ibl_atmosphere = frame_pipe::get_atmosphere();
        m_ibl_bake_time  = now;
        m_ibl_baked      = true;
        m_ibl_dirty      = false;
    }

    m_luts_dirty = false;
}

VkDeviceAddress get_params_address()
{
    return m_params_buffers[vk::frame::get_current_frame_index()].address;
}

bool is_active() { return m_active; }

const image& get_transmittance_image()    { return m_transmittance; }
const image& get_multi_scattering_image() { return m_multi_scattering; }
const image& get_sky_view_image()         { return m_sky_view; }

}
