#include "glp/glaze/rendering/scene_render/fsr.h"

#include "glp/glaze/rendering/scene_render/exposure.h"
#include "glp/glaze/rendering/scene_render/frame_pipeline.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/resources.h"
#include "glp/glaze/rendering/vk/samplers.h"

#include "glp/glaze/rendering/defines.h"

#include "glp/glaze/utilities/core/timer.h"

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace glaze::rendering::scene_render::fsr
{

namespace
{

struct fsr_constants
{
    int32_t render_size[2];
    int32_t previous_render_size[2];

    int32_t upscale_size[2];
    int32_t previous_upscale_size[2];

    int32_t max_render_size[2];
    int32_t max_upscale_size[2];

    float device_to_view_depth[4];

    float jitter_offset[2];
    float previous_jitter_offset[2];

    float motion_vector_scale[2];
    float downscale_factor[2];

    float motion_vector_jitter_cancellation[2];
    float tan_half_fov;
    float jitter_phase_count;

    float delta_time;
    float delta_pre_exposure;
    float view_space_to_meters_factor;
    float frame_index;

    float velocity_factor;
    float reactiveness_scale;
    float shading_change_scale;
    float accumulation_added_per_frame;
    float min_disocclusion_accumulation;
};

struct spd_constants
{
    uint32_t mips;
    uint32_t num_work_groups;
    uint32_t work_group_offset[2];
    uint32_t render_size[2];
};

struct sky_push
{
    glm::mat4 sky_reprojection;
};

enum class resource : uint32_t
{
    input_color,
    input_depth,
    input_motion_vectors,
    input_exposure,
    reactive_mask,
    transparency_mask,
    dilated_motion_vectors,
    dilated_depth,
    reconstructed_previous_depth,
    farthest_depth,
    current_luma,
    previous_luma,
    spd_atomic,
    frame_info,
    spd_mip0,
    spd_mip1,
    spd_mip2,
    spd_mip3,
    spd_mip4,
    spd_mip5,
    spd_mips,
    farthest_depth_mip1,
    luma_instability,
    dilated_reactive_masks,
    new_locks,
    accumulation_read,
    accumulation_write,
    shading_change,
    internal_upscaled_read,
    internal_upscaled_write,
    lanczos_lut,
    luma_history_read,
    luma_history_write,
    upscaled_output
};

enum class binding_kind
{
    sampled,
    storage,
    constants,
    spd_constants
};

struct pass_binding
{
    uint32_t binding;
    binding_kind kind;
    resource target;
};

struct pass_desc
{
    const char* name;
    const char* file;
    std::vector<pass_binding> bindings;
};

enum pass_index : uint32_t
{
    PASS_PREPARE_INPUTS,
    PASS_LUMA_PYRAMID,
    PASS_SHADING_CHANGE_PYRAMID,
    PASS_SHADING_CHANGE,
    PASS_PREPARE_REACTIVITY,
    PASS_LUMA_INSTABILITY,
    PASS_ACCUMULATE,
    PASS_COUNT
};

constexpr uint32_t PARITY_COUNT = 2;

constexpr uint32_t POINT_SAMPLER_BINDING  = 1000;
constexpr uint32_t LINEAR_SAMPLER_BINDING = 1001;

constexpr uint32_t MAX_PASS_SAMPLED   = 9;
constexpr uint32_t MAX_PASS_STORAGE   = 11;
constexpr uint32_t MAX_PASS_CONSTANTS = 2;
constexpr uint32_t PASS_SAMPLERS      = 2;

constexpr uint32_t SPD_MIP_VIEWS = 6;

constexpr uint32_t GROUP_SIZE     = 8;
constexpr uint32_t SPD_TILE_SIZE  = 64;
constexpr uint32_t SPD_MAX_MIPS   = 12;

constexpr uint32_t LANCZOS_LUT_WIDTH = 128;

constexpr float LANCZOS_EPSILON = 1e-6f;
constexpr float SNORM16_MAX     = 32767.0f;

constexpr float BASE_JITTER_PHASE_COUNT = 8.0f;

constexpr float ACCUMULATION_ADDED_PER_FRAME  = 1.0f / 3.0f;
constexpr float MIN_DISOCCLUSION_ACCUMULATION = -1.0f / 3.0f;

constexpr float SECONDS_TO_MS = 1000.0f;

constexpr float MIN_PROJECTION_VALUE = 0.0001f;

constexpr float TEXTURE_LOD_BIAS_OFFSET = 0.0f;

constexpr float PI = 3.14159265359f;

const std::vector<std::string> SHADER_DEFINES = {
    "FFX_GPU=1",
    "FFX_GLSL=1",
    "FFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0",
    "FFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0",
    "FFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1",
    "FFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0",
    "FFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2",
    "FFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE=0",
    "FFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT=1",
    "FFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS=1",
    "FFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS=0",
    "FFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH=1",
    "FFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING=0"
};

const std::array<pass_desc, PASS_COUNT> PASSES = {{
    {
        "fsr_prepare_inputs",
        "third_party/fidelityfx/ffx_fsr3upscaler_prepare_inputs_pass.glsl",
        {
            { 0, binding_kind::sampled,   resource::input_motion_vectors },
            { 1, binding_kind::sampled,   resource::input_depth },
            { 2, binding_kind::sampled,   resource::input_color },
            { 3, binding_kind::storage,   resource::dilated_motion_vectors },
            { 4, binding_kind::storage,   resource::dilated_depth },
            { 5, binding_kind::storage,   resource::reconstructed_previous_depth },
            { 6, binding_kind::storage,   resource::farthest_depth },
            { 7, binding_kind::storage,   resource::current_luma },
            { 8, binding_kind::constants, resource::input_color }
        }
    },
    {
        "fsr_luma_pyramid",
        "third_party/fidelityfx/ffx_fsr3upscaler_luma_pyramid_pass.glsl",
        {
            { 0,  binding_kind::sampled,       resource::current_luma },
            { 1,  binding_kind::sampled,       resource::farthest_depth },
            { 2,  binding_kind::storage,       resource::spd_atomic },
            { 3,  binding_kind::storage,       resource::frame_info },
            { 4,  binding_kind::storage,       resource::spd_mip0 },
            { 5,  binding_kind::storage,       resource::spd_mip1 },
            { 6,  binding_kind::storage,       resource::spd_mip2 },
            { 7,  binding_kind::storage,       resource::spd_mip3 },
            { 8,  binding_kind::storage,       resource::spd_mip4 },
            { 9,  binding_kind::storage,       resource::spd_mip5 },
            { 10, binding_kind::storage,       resource::farthest_depth_mip1 },
            { 11, binding_kind::constants,     resource::input_color },
            { 12, binding_kind::spd_constants, resource::input_color }
        }
    },
    {
        "fsr_shading_change_pyramid",
        "third_party/fidelityfx/ffx_fsr3upscaler_shading_change_pyramid_pass.glsl",
        {
            { 0,  binding_kind::sampled,       resource::current_luma },
            { 1,  binding_kind::sampled,       resource::previous_luma },
            { 2,  binding_kind::sampled,       resource::dilated_motion_vectors },
            { 3,  binding_kind::sampled,       resource::input_exposure },
            { 4,  binding_kind::storage,       resource::spd_atomic },
            { 5,  binding_kind::storage,       resource::spd_mip0 },
            { 6,  binding_kind::storage,       resource::spd_mip1 },
            { 7,  binding_kind::storage,       resource::spd_mip2 },
            { 8,  binding_kind::storage,       resource::spd_mip3 },
            { 9,  binding_kind::storage,       resource::spd_mip4 },
            { 10, binding_kind::storage,       resource::spd_mip5 },
            { 11, binding_kind::constants,     resource::input_color },
            { 12, binding_kind::spd_constants, resource::input_color }
        }
    },
    {
        "fsr_shading_change",
        "third_party/fidelityfx/ffx_fsr3upscaler_shading_change_pass.glsl",
        {
            { 0, binding_kind::sampled,   resource::spd_mips },
            { 1, binding_kind::storage,   resource::shading_change },
            { 2, binding_kind::constants, resource::input_color }
        }
    },
    {
        "fsr_prepare_reactivity",
        "third_party/fidelityfx/ffx_fsr3upscaler_prepare_reactivity_pass.glsl",
        {
            { 0,  binding_kind::sampled,   resource::reconstructed_previous_depth },
            { 1,  binding_kind::sampled,   resource::dilated_motion_vectors },
            { 2,  binding_kind::sampled,   resource::dilated_depth },
            { 3,  binding_kind::sampled,   resource::reactive_mask },
            { 4,  binding_kind::sampled,   resource::transparency_mask },
            { 5,  binding_kind::sampled,   resource::accumulation_read },
            { 6,  binding_kind::sampled,   resource::shading_change },
            { 7,  binding_kind::sampled,   resource::current_luma },
            { 8,  binding_kind::sampled,   resource::input_exposure },
            { 9,  binding_kind::storage,   resource::dilated_reactive_masks },
            { 10, binding_kind::storage,   resource::new_locks },
            { 11, binding_kind::storage,   resource::accumulation_write },
            { 12, binding_kind::constants, resource::input_color }
        }
    },
    {
        "fsr_luma_instability",
        "third_party/fidelityfx/ffx_fsr3upscaler_luma_instability_pass.glsl",
        {
            { 0, binding_kind::sampled,   resource::input_exposure },
            { 1, binding_kind::sampled,   resource::dilated_reactive_masks },
            { 2, binding_kind::sampled,   resource::dilated_motion_vectors },
            { 3, binding_kind::sampled,   resource::frame_info },
            { 4, binding_kind::sampled,   resource::luma_history_read },
            { 5, binding_kind::sampled,   resource::farthest_depth_mip1 },
            { 6, binding_kind::sampled,   resource::current_luma },
            { 7, binding_kind::storage,   resource::luma_history_write },
            { 8, binding_kind::storage,   resource::luma_instability },
            { 9, binding_kind::constants, resource::input_color }
        }
    },
    {
        "fsr_accumulate",
        "third_party/fidelityfx/ffx_fsr3upscaler_accumulate_pass.glsl",
        {
            { 0,  binding_kind::sampled,   resource::input_exposure },
            { 1,  binding_kind::sampled,   resource::dilated_reactive_masks },
            { 2,  binding_kind::sampled,   resource::dilated_motion_vectors },
            { 3,  binding_kind::sampled,   resource::internal_upscaled_read },
            { 4,  binding_kind::sampled,   resource::lanczos_lut },
            { 5,  binding_kind::sampled,   resource::farthest_depth_mip1 },
            { 6,  binding_kind::sampled,   resource::current_luma },
            { 7,  binding_kind::sampled,   resource::luma_instability },
            { 8,  binding_kind::sampled,   resource::input_color },
            { 9,  binding_kind::storage,   resource::internal_upscaled_write },
            { 10, binding_kind::storage,   resource::upscaled_output },
            { 11, binding_kind::storage,   resource::new_locks },
            { 12, binding_kind::constants, resource::input_color }
        }
    }
}};

struct internal_images
{
    image accumulation[PARITY_COUNT];
    image luma[PARITY_COUNT];
    image luma_history[PARITY_COUNT];
    image internal_upscaled[PARITY_COUNT];

    image intermediate;
    image shading_change;
    image new_locks;
    image spd_mips;
    image farthest_depth_mip1;
    image spd_atomic;
    image dilated_reactive_masks;
    image dilated_depth;
    image dilated_motion_vectors;
    image reconstructed_previous_depth;
    image frame_info;

    VkImageView spd_mip_views[SPD_MIP_VIEWS] = {};
};

internal_images m_images;

image m_lanczos_lut;
image m_default_reactivity;
image m_default_exposure;

VkExtent2D m_render_extent  = {};
VkExtent2D m_display_extent = {};

bool m_images_created = false;
bool m_images_initialized = false;

VkDescriptorSetLayout m_pass_layouts[PASS_COUNT] = {};
VkDescriptorPool m_pool = VK_NULL_HANDLE;
VkDescriptorSet m_sets[MAX_FRAMES_IN_FLIGHT][PARITY_COUNT][PASS_COUNT] = {};

const shader* m_pass_shaders[PASS_COUNT] = {};

buffer m_constant_buffers[MAX_FRAMES_IN_FLIGHT];
buffer m_spd_buffers[MAX_FRAMES_IN_FLIGHT];

VkSampler m_point_sampler  = VK_NULL_HANDLE;
VkSampler m_linear_sampler = VK_NULL_HANDLE;

VkDescriptorSetLayout m_sky_layout = VK_NULL_HANDLE;
VkDescriptorPool m_sky_pool = VK_NULL_HANDLE;
VkDescriptorSet m_sky_set = VK_NULL_HANDLE;

const shader* m_sky_shader = nullptr;

VkImageView m_written_color_view    = VK_NULL_HANDLE;
VkImageView m_written_depth_view    = VK_NULL_HANDLE;
VkImageView m_written_velocity_view = VK_NULL_HANDLE;
VkImageView m_written_output_view   = VK_NULL_HANDLE;

fsr_constants m_constants = {};

bool m_first_execution = true;
bool m_reset_requested = true;

uint32_t m_resource_frame = 0;

float m_pre_exposure          = 0.0f;
float m_previous_pre_exposure = 0.0f;

glm::mat4 m_previous_view_proj = glm::mat4(1.0f);
bool m_has_previous_view_proj  = false;

float halton(uint32_t index, uint32_t base)
{
    float result   = 0.0f;
    float fraction = 1.0f;

    for (; index > 0; index /= base) {
        fraction /= static_cast<float>(base);
        result   += fraction * static_cast<float>(index % base);
    }

    return result;
}

int32_t jitter_phase_count(uint32_t render_width, uint32_t display_width)
{
    const float ratio = static_cast<float>(display_width) / static_cast<float>(std::max(render_width, 1U));

    return static_cast<int32_t>(BASE_JITTER_PHASE_COUNT * ratio * ratio);
}

float lanczos2(float value)
{
    if (std::abs(value) < LANCZOS_EPSILON) {
        return 1.0f;
    }

    return (std::sin(PI * value) / (PI * value)) * (std::sin(0.5f * PI * value) / (0.5f * PI * value));
}

uint32_t mip_count(VkExtent2D extent)
{
    return static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(extent.width, extent.height))))) + 1;
}

image make_image(VkExtent2D extent, VkFormat format, uint32_t mips = 1)
{
    return vk::resources::create_image(
        { std::max(extent.width, 1U), std::max(extent.height, 1U), 1 },
        format,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        mips
    );
}

image make_constant_image(VkExtent2D extent, VkFormat format, const void* data, VkDeviceSize size)
{
    image result = vk::resources::create_image(
        { extent.width, extent.height, 1 },
        format,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    vk::resources::queue_image_upload(result, data, size);

    return result;
}

void create_constant_images()
{
    std::array<int16_t, LANCZOS_LUT_WIDTH> lanczos = {};

    for (uint32_t i = 0; i < LANCZOS_LUT_WIDTH; i++) {
        const float x = 2.0f * static_cast<float>(i) / static_cast<float>(LANCZOS_LUT_WIDTH - 1);

        lanczos[i] = static_cast<int16_t>(std::round(lanczos2(x) * SNORM16_MAX));
    }

    const uint8_t default_reactivity = 0;
    const float default_exposure[2] = { 0.0f, 0.0f };

    m_lanczos_lut        = make_constant_image({ LANCZOS_LUT_WIDTH, 1 }, VK_FORMAT_R16_SNORM, lanczos.data(), sizeof(lanczos));
    m_default_reactivity = make_constant_image({ 1, 1 }, VK_FORMAT_R8_UNORM, &default_reactivity, sizeof(default_reactivity));
    m_default_exposure   = make_constant_image({ 1, 1 }, VK_FORMAT_R32G32_SFLOAT, default_exposure, sizeof(default_exposure));
}

void destroy_constant_images()
{
    vk::resources::destroy_image(m_default_exposure);
    vk::resources::destroy_image(m_default_reactivity);
    vk::resources::destroy_image(m_lanczos_lut);
}

void create_images(VkExtent2D render, VkExtent2D display)
{
    const VkExtent2D half = { std::max(render.width / 2, 1U), std::max(render.height / 2, 1U) };

    for (uint32_t i = 0; i < PARITY_COUNT; i++) {
        m_images.accumulation[i]      = make_image(render, VK_FORMAT_R8_UNORM);
        m_images.luma[i]              = make_image(render, VK_FORMAT_R16_SFLOAT);
        m_images.luma_history[i]      = make_image(render, VK_FORMAT_R16G16B16A16_SFLOAT);
        m_images.internal_upscaled[i] = make_image(display, VK_FORMAT_R16G16B16A16_SFLOAT);
    }

    m_images.intermediate                 = make_image(render, VK_FORMAT_R16_SFLOAT);
    m_images.shading_change               = make_image(half, VK_FORMAT_R8_UNORM);
    m_images.new_locks                    = make_image(display, VK_FORMAT_R8_UNORM);
    m_images.spd_mips                     = make_image(half, VK_FORMAT_R16G16_SFLOAT, mip_count(half));
    m_images.farthest_depth_mip1          = make_image(half, VK_FORMAT_R16_SFLOAT);
    m_images.spd_atomic                   = make_image({ 1, 1 }, VK_FORMAT_R32_UINT);
    m_images.dilated_reactive_masks       = make_image(render, VK_FORMAT_R8G8B8A8_UNORM);
    m_images.dilated_depth                = make_image(render, VK_FORMAT_R32_SFLOAT);
    m_images.dilated_motion_vectors       = make_image(render, VK_FORMAT_R16G16_SFLOAT);
    m_images.reconstructed_previous_depth = make_image(render, VK_FORMAT_R32_UINT);
    m_images.frame_info                   = make_image({ 1, 1 }, VK_FORMAT_R32G32B32A32_SFLOAT);

    for (uint32_t mip = 0; mip < SPD_MIP_VIEWS; mip++) {
        const uint32_t level = std::min(mip, m_images.spd_mips.mip_levels - 1);

        m_images.spd_mip_views[mip] = vk::resources::create_image_view(m_images.spd_mips, VK_IMAGE_VIEW_TYPE_2D, level, 1);
    }

    m_render_extent  = render;
    m_display_extent = display;

    m_images_created     = true;
    m_images_initialized = false;
}

void destroy_images()
{
    if (!m_images_created) {
        return;
    }

    for (VkImageView& view : m_images.spd_mip_views) {
        vk::resources::destroy_image_view(view);

        view = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0; i < PARITY_COUNT; i++) {
        vk::resources::destroy_image(m_images.accumulation[i]);
        vk::resources::destroy_image(m_images.luma[i]);
        vk::resources::destroy_image(m_images.luma_history[i]);
        vk::resources::destroy_image(m_images.internal_upscaled[i]);
    }

    vk::resources::destroy_image(m_images.intermediate);
    vk::resources::destroy_image(m_images.shading_change);
    vk::resources::destroy_image(m_images.new_locks);
    vk::resources::destroy_image(m_images.spd_mips);
    vk::resources::destroy_image(m_images.farthest_depth_mip1);
    vk::resources::destroy_image(m_images.spd_atomic);
    vk::resources::destroy_image(m_images.dilated_reactive_masks);
    vk::resources::destroy_image(m_images.dilated_depth);
    vk::resources::destroy_image(m_images.dilated_motion_vectors);
    vk::resources::destroy_image(m_images.reconstructed_previous_depth);
    vk::resources::destroy_image(m_images.frame_info);

    m_images_created = false;
}

std::vector<const image*> all_internal_images()
{
    std::vector<const image*> result;

    for (uint32_t i = 0; i < PARITY_COUNT; i++) {
        result.push_back(&m_images.accumulation[i]);
        result.push_back(&m_images.luma[i]);
        result.push_back(&m_images.luma_history[i]);
        result.push_back(&m_images.internal_upscaled[i]);
    }

    result.push_back(&m_images.intermediate);
    result.push_back(&m_images.shading_change);
    result.push_back(&m_images.new_locks);
    result.push_back(&m_images.spd_mips);
    result.push_back(&m_images.farthest_depth_mip1);
    result.push_back(&m_images.spd_atomic);
    result.push_back(&m_images.dilated_reactive_masks);
    result.push_back(&m_images.dilated_depth);
    result.push_back(&m_images.dilated_motion_vectors);
    result.push_back(&m_images.reconstructed_previous_depth);
    result.push_back(&m_images.frame_info);

    return result;
}

VkDescriptorType descriptor_type(binding_kind kind)
{
    switch (kind) {
        case binding_kind::sampled:       return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case binding_kind::storage:       return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case binding_kind::constants:     return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case binding_kind::spd_constants: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }

    return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
}

void create_pass_layouts()
{
    for (uint32_t pass = 0; pass < PASS_COUNT; pass++) {
        std::vector<VkDescriptorSetLayoutBinding> bindings;

        for (const pass_binding& target : PASSES[pass].bindings) {
            bindings.push_back({ target.binding, descriptor_type(target.kind), 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
        }

        bindings.push_back({ POINT_SAMPLER_BINDING,  VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
        bindings.push_back({ LINEAR_SAMPLER_BINDING, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });

        m_pass_layouts[pass] = vk::descriptors::create_set_layout(bindings);
    }

    const uint32_t set_count = MAX_FRAMES_IN_FLIGHT * PARITY_COUNT * PASS_COUNT;

    m_pool = vk::descriptors::create_pool(
        set_count,
        {
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  set_count * MAX_PASS_SAMPLED },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  set_count * MAX_PASS_STORAGE },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, set_count * MAX_PASS_CONSTANTS },
            { VK_DESCRIPTOR_TYPE_SAMPLER,        set_count * PASS_SAMPLERS }
        }
    );

    for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; frame++) {
        for (uint32_t parity = 0; parity < PARITY_COUNT; parity++) {
            for (uint32_t pass = 0; pass < PASS_COUNT; pass++) {
                m_sets[frame][parity][pass] = vk::descriptors::allocate_set(m_pool, m_pass_layouts[pass]);
            }
        }
    }
}

void create_pass_shaders()
{
    for (uint32_t pass = 0; pass < PASS_COUNT; pass++) {
        compute_pipeline_create_info info;
        info.set_layouts = { m_pass_layouts[pass] };
        info.defines     = SHADER_DEFINES;

        m_pass_shaders[pass] = res::shaders::build(PASSES[pass].name, "", "", PASSES[pass].file, {}, info, render_queue::opaque);
    }
}

void create_sky_resources()
{
    m_sky_layout = vk::descriptors::create_set_layout({
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    });

    m_sky_pool = vk::descriptors::create_pool(
        1,
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1 }
        }
    );

    m_sky_set = vk::descriptors::allocate_set(m_sky_pool, m_sky_layout);

    compute_pipeline_create_info info;
    info.set_layouts        = { m_sky_layout };
    info.push_constant_size = sizeof(sky_push);

    m_sky_shader = res::shaders::build("fsr_sky_velocity", "", "", "sys/post/sky_velocity.comp", {}, info, render_queue::opaque);
}

VkSampler make_sampler(VkFilter filter, VkSamplerMipmapMode mip_mode)
{
    sampler_create_info sampler_info;
    sampler_info.mag_filter = filter;
    sampler_info.min_filter = filter;
    sampler_info.mip_mode   = mip_mode;
    sampler_info.wrap_u     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_v     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_w     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropy = false;

    return vk::samplers::get(vk::samplers::make(sampler_info));
}

struct resolved_view
{
    VkImageView view;
    VkImageLayout layout;
};

resolved_view resolve(resource target, uint32_t parity)
{
    const uint32_t read  = parity;
    const uint32_t write = 1 - parity;

    switch (target) {
        case resource::input_color:                  return { it::get_hdr_image().view,                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        case resource::input_depth:                  return { it::get_depth_image().view,                   VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL };
        case resource::input_motion_vectors:         return { it::get_velocity_image().view,                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        case resource::input_exposure:               return { m_default_exposure.view,                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        case resource::reactive_mask:                return { m_default_reactivity.view,                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        case resource::transparency_mask:            return { m_default_reactivity.view,                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        case resource::lanczos_lut:                  return { m_lanczos_lut.view,                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        case resource::upscaled_output:              return { it::get_output_image().view,                  VK_IMAGE_LAYOUT_GENERAL };
        case resource::dilated_motion_vectors:       return { m_images.dilated_motion_vectors.view,         VK_IMAGE_LAYOUT_GENERAL };
        case resource::dilated_depth:                return { m_images.dilated_depth.view,                  VK_IMAGE_LAYOUT_GENERAL };
        case resource::reconstructed_previous_depth: return { m_images.reconstructed_previous_depth.view,   VK_IMAGE_LAYOUT_GENERAL };
        case resource::farthest_depth:               return { m_images.intermediate.view,                   VK_IMAGE_LAYOUT_GENERAL };
        case resource::luma_instability:             return { m_images.intermediate.view,                   VK_IMAGE_LAYOUT_GENERAL };
        case resource::current_luma:                 return { m_images.luma[read].view,                     VK_IMAGE_LAYOUT_GENERAL };
        case resource::previous_luma:                return { m_images.luma[write].view,                    VK_IMAGE_LAYOUT_GENERAL };
        case resource::spd_atomic:                   return { m_images.spd_atomic.view,                     VK_IMAGE_LAYOUT_GENERAL };
        case resource::frame_info:                   return { m_images.frame_info.view,                     VK_IMAGE_LAYOUT_GENERAL };
        case resource::spd_mip0:                     return { m_images.spd_mip_views[0],                    VK_IMAGE_LAYOUT_GENERAL };
        case resource::spd_mip1:                     return { m_images.spd_mip_views[1],                    VK_IMAGE_LAYOUT_GENERAL };
        case resource::spd_mip2:                     return { m_images.spd_mip_views[2],                    VK_IMAGE_LAYOUT_GENERAL };
        case resource::spd_mip3:                     return { m_images.spd_mip_views[3],                    VK_IMAGE_LAYOUT_GENERAL };
        case resource::spd_mip4:                     return { m_images.spd_mip_views[4],                    VK_IMAGE_LAYOUT_GENERAL };
        case resource::spd_mip5:                     return { m_images.spd_mip_views[5],                    VK_IMAGE_LAYOUT_GENERAL };
        case resource::spd_mips:                     return { m_images.spd_mips.view,                       VK_IMAGE_LAYOUT_GENERAL };
        case resource::farthest_depth_mip1:          return { m_images.farthest_depth_mip1.view,            VK_IMAGE_LAYOUT_GENERAL };
        case resource::dilated_reactive_masks:       return { m_images.dilated_reactive_masks.view,         VK_IMAGE_LAYOUT_GENERAL };
        case resource::new_locks:                    return { m_images.new_locks.view,                      VK_IMAGE_LAYOUT_GENERAL };
        case resource::accumulation_read:            return { m_images.accumulation[read].view,             VK_IMAGE_LAYOUT_GENERAL };
        case resource::accumulation_write:           return { m_images.accumulation[write].view,            VK_IMAGE_LAYOUT_GENERAL };
        case resource::shading_change:               return { m_images.shading_change.view,                 VK_IMAGE_LAYOUT_GENERAL };
        case resource::internal_upscaled_read:       return { m_images.internal_upscaled[read].view,        VK_IMAGE_LAYOUT_GENERAL };
        case resource::internal_upscaled_write:      return { m_images.internal_upscaled[write].view,       VK_IMAGE_LAYOUT_GENERAL };
        case resource::luma_history_read:            return { m_images.luma_history[read].view,             VK_IMAGE_LAYOUT_GENERAL };
        case resource::luma_history_write:           return { m_images.luma_history[write].view,            VK_IMAGE_LAYOUT_GENERAL };
    }

    return { VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL };
}

void write_sets()
{
    for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; frame++) {
        for (uint32_t parity = 0; parity < PARITY_COUNT; parity++) {
            for (uint32_t pass = 0; pass < PASS_COUNT; pass++) {
                const VkDescriptorSet set = m_sets[frame][parity][pass];

                for (const pass_binding& target : PASSES[pass].bindings) {
                    if (target.kind == binding_kind::constants) {
                        vk::descriptors::write_buffer(set, target.binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, m_constant_buffers[frame]);
                        continue;
                    }

                    if (target.kind == binding_kind::spd_constants) {
                        vk::descriptors::write_buffer(set, target.binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, m_spd_buffers[frame]);
                        continue;
                    }

                    const resolved_view resolved = resolve(target.target, parity);

                    vk::descriptors::write_image(set, target.binding, descriptor_type(target.kind), resolved.view, resolved.layout);
                }

                vk::descriptors::write_image(set, POINT_SAMPLER_BINDING, VK_DESCRIPTOR_TYPE_SAMPLER, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, m_point_sampler);
                vk::descriptors::write_image(set, LINEAR_SAMPLER_BINDING, VK_DESCRIPTOR_TYPE_SAMPLER, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, m_linear_sampler);
            }
        }
    }

    vk::descriptors::write_image(m_sky_set, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, it::get_depth_image().view, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, m_point_sampler);
    vk::descriptors::write_image(m_sky_set, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, it::get_velocity_image().view, VK_IMAGE_LAYOUT_GENERAL);
}

void spd_setup(VkExtent2D extent, uint32_t groups[2], spd_constants& constants)
{
    const uint32_t end_x = (extent.width - 1) / SPD_TILE_SIZE;
    const uint32_t end_y = (extent.height - 1) / SPD_TILE_SIZE;

    groups[0] = end_x + 1;
    groups[1] = end_y + 1;

    constants.num_work_groups      = groups[0] * groups[1];
    constants.mips                 = std::min(static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(extent.width, extent.height))))), SPD_MAX_MIPS);
    constants.work_group_offset[0] = 0;
    constants.work_group_offset[1] = 0;
    constants.render_size[0]       = extent.width;
    constants.render_size[1]       = extent.height;
}

void update_constants(bool reset)
{
    const gpu_camera& camera = frame_pipe::get_camera();

    const float near_plane   = std::max(camera.proj[3][2], MIN_PROJECTION_VALUE);
    const float cot_half_fov = std::max(std::abs(camera.proj[1][1]), MIN_PROJECTION_VALUE);
    const float fov_vertical = 2.0f * std::atan(1.0f / cot_half_fov);

    const float aspect = static_cast<float>(m_render_extent.width) / static_cast<float>(m_render_extent.height);

    const glm::vec2 jitter = it::get_jitter_offset();

    m_constants.previous_jitter_offset[0] = m_constants.jitter_offset[0];
    m_constants.previous_jitter_offset[1] = m_constants.jitter_offset[1];
    m_constants.jitter_offset[0] = jitter.x;
    m_constants.jitter_offset[1] = jitter.y;

    m_constants.previous_render_size[0] = m_constants.render_size[0];
    m_constants.previous_render_size[1] = m_constants.render_size[1];
    m_constants.render_size[0] = static_cast<int32_t>(m_render_extent.width);
    m_constants.render_size[1] = static_cast<int32_t>(m_render_extent.height);
    m_constants.max_render_size[0] = static_cast<int32_t>(m_render_extent.width);
    m_constants.max_render_size[1] = static_cast<int32_t>(m_render_extent.height);

    const float fov_horizontal = std::atan(std::tan(fov_vertical * 0.5f) * aspect) * 2.0f;

    m_constants.tan_half_fov                = std::tan(fov_horizontal * 0.5f);
    m_constants.view_space_to_meters_factor = 1.0f;

    m_constants.device_to_view_depth[0] = -FLT_EPSILON;
    m_constants.device_to_view_depth[1] = near_plane;
    m_constants.device_to_view_depth[2] = aspect / cot_half_fov;
    m_constants.device_to_view_depth[3] = 1.0f / cot_half_fov;

    m_constants.previous_upscale_size[0] = m_constants.upscale_size[0];
    m_constants.previous_upscale_size[1] = m_constants.upscale_size[1];
    m_constants.upscale_size[0] = static_cast<int32_t>(m_display_extent.width);
    m_constants.upscale_size[1] = static_cast<int32_t>(m_display_extent.height);
    m_constants.max_upscale_size[0] = static_cast<int32_t>(m_display_extent.width);
    m_constants.max_upscale_size[1] = static_cast<int32_t>(m_display_extent.height);

    m_constants.downscale_factor[0] = static_cast<float>(m_constants.render_size[0]) / static_cast<float>(m_constants.upscale_size[0]);
    m_constants.downscale_factor[1] = static_cast<float>(m_constants.render_size[1]) / static_cast<float>(m_constants.upscale_size[1]);

    m_constants.delta_pre_exposure = 1.0f;

    const float exposure = exposure::get_exposure();

    m_previous_pre_exposure = m_pre_exposure;
    m_pre_exposure          = exposure != 0.0f ? exposure : 1.0f;

    if (m_previous_pre_exposure > 0.0f) {
        m_constants.delta_pre_exposure = m_pre_exposure / m_previous_pre_exposure;
    }

    m_constants.motion_vector_scale[0] = -1.0f;
    m_constants.motion_vector_scale[1] = -1.0f;

    m_constants.motion_vector_jitter_cancellation[0] = 0.0f;
    m_constants.motion_vector_jitter_cancellation[1] = 0.0f;

    const int32_t phase_count = jitter_phase_count(m_render_extent.width, m_display_extent.width);

    if (reset || m_constants.jitter_phase_count == 0.0f) {
        m_constants.jitter_phase_count = static_cast<float>(phase_count);
    } else {
        const int32_t delta = phase_count - static_cast<int32_t>(m_constants.jitter_phase_count);

        if (delta > 0) {
            m_constants.jitter_phase_count += 1.0f;
        } else if (delta < 0) {
            m_constants.jitter_phase_count -= 1.0f;
        }
    }

    m_constants.delta_time  = std::clamp(utilities::core::timer::delta_ms / SECONDS_TO_MS, 0.0f, 1.0f);
    m_constants.frame_index = reset ? 0.0f : m_constants.frame_index + 1.0f;

    m_constants.velocity_factor               = 1.0f;
    m_constants.reactiveness_scale            = 1.0f;
    m_constants.shading_change_scale          = 1.0f;
    m_constants.accumulation_added_per_frame  = ACCUMULATION_ADDED_PER_FRAME;
    m_constants.min_disocclusion_accumulation = MIN_DISOCCLUSION_ACCUMULATION;
}

void clear_image(VkCommandBuffer cmd, const image& target, VkClearColorValue value)
{
    VkImageSubresourceRange range = {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = target.mip_levels;
    range.layerCount = 1;

    vk::device::get_disp().cmdClearColorImage(cmd, target.handle, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
}

VkClearColorValue clear_float(float r, float g, float b, float a)
{
    VkClearColorValue value = {};
    value.float32[0] = r;
    value.float32[1] = g;
    value.float32[2] = b;
    value.float32[3] = a;

    return value;
}

VkClearColorValue clear_uint(uint32_t v)
{
    VkClearColorValue value = {};
    value.uint32[0] = v;
    value.uint32[1] = v;
    value.uint32[2] = v;
    value.uint32[3] = v;

    return value;
}

void compute_barrier(VkCommandBuffer cmd)
{
    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );
}

void prepare_inputs(VkCommandBuffer cmd)
{
    const image& depth    = it::get_depth_image();
    const image& hdr      = it::get_hdr_image();
    const image& velocity = it::get_velocity_image();

    vk::commands::transition_image(
        cmd,
        depth.handle,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    vk::commands::transition_image(
        cmd,
        hdr.handle,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );

    vk::commands::transition_image(
        cmd,
        velocity.handle,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    const glm::mat4 view_proj = frame_pipe::has_camera() ? frame_pipe::get_camera().view_proj : glm::mat4(1.0f);

    sky_push push;
    push.sky_reprojection = (m_has_previous_view_proj ? m_previous_view_proj : view_proj) * glm::inverse(view_proj);

    m_previous_view_proj     = view_proj;
    m_has_previous_view_proj = true;

    vk::device::get_disp().cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_sky_shader->compute.layout, 0, 1, &m_sky_set, 0, nullptr);

    vk::commands::dispatch(
        cmd,
        m_sky_shader->compute,
        &push,
        sizeof(push),
        (velocity.extent.width + GROUP_SIZE - 1) / GROUP_SIZE,
        (velocity.extent.height + GROUP_SIZE - 1) / GROUP_SIZE,
        1
    );

    vk::commands::transition_image(
        cmd,
        velocity.handle,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );

    vk::commands::transition_image(
        cmd,
        it::get_output_image().handle,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        0,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );
}

void initialize_images(VkCommandBuffer cmd)
{
    for (const image* target : all_internal_images()) {
        vk::commands::transition_image(
            cmd,
            target->handle,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_NONE,
            0,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            target->mip_levels
        );
    }

    m_images_initialized = true;
}

void run_pass(VkCommandBuffer cmd, uint32_t pass, uint32_t parity, uint32_t groups_x, uint32_t groups_y)
{
    const VkDescriptorSet set = m_sets[vk::frame::get_current_frame_index()][parity][pass];

    vk::device::get_disp().cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pass_shaders[pass]->compute.layout, 0, 1, &set, 0, nullptr);

    vk::commands::dispatch(cmd, m_pass_shaders[pass]->compute, nullptr, 0, groups_x, groups_y, 1);

    compute_barrier(cmd);
}

}

void init_all()
{
    m_point_sampler  = make_sampler(VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST);
    m_linear_sampler = make_sampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_constant_buffers[i] = vk::resources::create_buffer(sizeof(fsr_constants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_spd_buffers[i]      = vk::resources::create_buffer(sizeof(spd_constants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    }

    create_constant_images();
    create_pass_layouts();
    create_pass_shaders();
    create_sky_resources();

    m_reset_requested = true;
}

void clean_all()
{
    destroy_images();
    destroy_constant_images();

    vk::descriptors::destroy_pool(m_pool);

    for (VkDescriptorSetLayout& layout : m_pass_layouts) {
        vk::descriptors::destroy_set_layout(layout);

        layout = VK_NULL_HANDLE;
    }

    m_pool = VK_NULL_HANDLE;

    vk::descriptors::destroy_pool(m_sky_pool);
    vk::descriptors::destroy_set_layout(m_sky_layout);

    m_sky_pool   = VK_NULL_HANDLE;
    m_sky_layout = VK_NULL_HANDLE;
    m_sky_set    = VK_NULL_HANDLE;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::resources::destroy_buffer(m_spd_buffers[i]);
        vk::resources::destroy_buffer(m_constant_buffers[i]);
    }

    for (const shader*& target : m_pass_shaders) {
        target = nullptr;
    }

    m_sky_shader = nullptr;

    m_point_sampler  = VK_NULL_HANDLE;
    m_linear_sampler = VK_NULL_HANDLE;

    m_written_color_view    = VK_NULL_HANDLE;
    m_written_depth_view    = VK_NULL_HANDLE;
    m_written_velocity_view = VK_NULL_HANDLE;
    m_written_output_view   = VK_NULL_HANDLE;

    m_constants = {};

    m_has_previous_view_proj = false;
}

void handle_all()
{
    const VkExtent3D render_size  = it::get_depth_image().extent;
    const VkExtent3D display_size = it::get_output_image().extent;

    const VkExtent2D render  = { render_size.width, render_size.height };
    const VkExtent2D display = { display_size.width, display_size.height };

    const bool resized = !m_images_created
        || render.width != m_render_extent.width || render.height != m_render_extent.height
        || display.width != m_display_extent.width || display.height != m_display_extent.height;

    if (resized) {
        vk::device::get_disp().deviceWaitIdle();

        destroy_images();
        create_images(render, display);

        m_first_execution = true;
    }

    const bool views_changed = it::get_hdr_image().view != m_written_color_view
        || it::get_depth_image().view != m_written_depth_view
        || it::get_velocity_image().view != m_written_velocity_view
        || it::get_output_image().view != m_written_output_view;

    if (resized || views_changed) {
        write_sets();

        m_written_color_view    = it::get_hdr_image().view;
        m_written_depth_view    = it::get_depth_image().view;
        m_written_velocity_view = it::get_velocity_image().view;
        m_written_output_view   = it::get_output_image().view;
    }
}

void dispatch()
{
    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    prepare_inputs(cmd);

    if (!m_images_initialized) {
        initialize_images(cmd);
    }

    const uint32_t parity = m_resource_frame & 1;

    const bool reset = m_reset_requested || m_first_execution;

    if (m_first_execution) {
        for (uint32_t i = 0; i < PARITY_COUNT; i++) {
            clear_image(cmd, m_images.accumulation[i], clear_float(0.0f, 0.0f, 0.0f, 0.0f));
            clear_image(cmd, m_images.luma[i], clear_float(0.0f, 0.0f, 0.0f, 0.0f));
        }
    }

    m_first_execution = false;
    m_reset_requested = false;

    if (reset) {
        clear_image(cmd, m_images.accumulation[parity], clear_float(0.0f, 0.0f, 0.0f, 0.0f));
        clear_image(cmd, m_images.frame_info, clear_float(-1.0f, 1.0f, 0.0f, 0.0f));
    }

    clear_image(cmd, m_images.reconstructed_previous_depth, clear_uint(0));
    clear_image(cmd, m_images.spd_atomic, clear_uint(0));
    clear_image(cmd, m_images.spd_mips, clear_float(0.0f, 0.0f, 0.0f, 0.0f));

    compute_barrier(cmd);

    update_constants(reset);

    uint32_t spd_groups[2] = {};
    spd_constants spd = {};

    spd_setup(m_render_extent, spd_groups, spd);

    const uint32_t frame = vk::frame::get_current_frame_index();

    vk::resources::upload_buffer(m_constant_buffers[frame], &m_constants, sizeof(m_constants));
    vk::resources::upload_buffer(m_spd_buffers[frame], &spd, sizeof(spd));

    const uint32_t source_x = (m_render_extent.width + GROUP_SIZE - 1) / GROUP_SIZE;
    const uint32_t source_y = (m_render_extent.height + GROUP_SIZE - 1) / GROUP_SIZE;

    const uint32_t target_x = (m_display_extent.width + GROUP_SIZE - 1) / GROUP_SIZE;
    const uint32_t target_y = (m_display_extent.height + GROUP_SIZE - 1) / GROUP_SIZE;

    const uint32_t shading_x = (m_render_extent.width / 2 + GROUP_SIZE - 1) / GROUP_SIZE;
    const uint32_t shading_y = (m_render_extent.height / 2 + GROUP_SIZE - 1) / GROUP_SIZE;

    run_pass(cmd, PASS_PREPARE_INPUTS, parity, source_x, source_y);
    run_pass(cmd, PASS_LUMA_PYRAMID, parity, spd_groups[0], spd_groups[1]);
    run_pass(cmd, PASS_SHADING_CHANGE_PYRAMID, parity, spd_groups[0], spd_groups[1]);
    run_pass(cmd, PASS_SHADING_CHANGE, parity, shading_x, shading_y);
    run_pass(cmd, PASS_PREPARE_REACTIVITY, parity, source_x, source_y);
    run_pass(cmd, PASS_LUMA_INSTABILITY, parity, source_x, source_y);
    run_pass(cmd, PASS_ACCUMULATE, parity, target_x, target_y);

    vk::commands::transition_image(
        cmd,
        it::get_output_image().handle,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );

    m_resource_frame++;
}

float get_upscale_ratio(uint32_t mode)
{
    switch (static_cast<upscale_mode>(mode)) {
        case upscale_mode::native_aa:         return 1.0f;
        case upscale_mode::quality:           return 1.5f;
        case upscale_mode::balanced:          return 1.7f;
        case upscale_mode::performance:       return 2.0f;
        case upscale_mode::ultra_performance: return 3.0f;
    }

    return 1.0f;
}

VkExtent2D get_render_extent(VkExtent2D display_extent, uint32_t mode)
{
    const float ratio = get_upscale_ratio(mode);

    return {
        std::max(1U, static_cast<uint32_t>(static_cast<float>(display_extent.width) / ratio)),
        std::max(1U, static_cast<uint32_t>(static_cast<float>(display_extent.height) / ratio))
    };
}

glm::vec2 get_jitter_offset(uint32_t frame_index, uint32_t render_width, uint32_t display_width)
{
    const uint32_t phase_count = static_cast<uint32_t>(std::max(jitter_phase_count(render_width, display_width), 1));

    const uint32_t sample = frame_index % phase_count + 1;

    return glm::vec2(halton(sample, 2) - 0.5f, halton(sample, 3) - 0.5f);
}

float get_texture_lod_bias(uint32_t render_width, uint32_t display_width)
{
    return std::log2(static_cast<float>(render_width) / static_cast<float>(std::max(display_width, 1U))) - TEXTURE_LOD_BIAS_OFFSET;
}

void request_reset()
{
    m_reset_requested = true;
}

}
