#include "glp/glaze/rendering/scene_render/exposure.h"

#include "glp/glaze/rendering/scene_render/clouds.h"
#include "glp/glaze/rendering/scene_render/frame_pipeline.h"
#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/gpu_post.h"
#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/descriptors.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/resources.h"
#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"

#include "glp/glaze/utilities/core/timer.h"

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace glaze::rendering::scene_render::exposure
{

namespace
{

struct histogram_push
{
    VkDeviceAddress histogram;
    VkDeviceAddress frame;
    float inverse_exposure;
    float min_log_luminance;
    float log_luminance_range;
    float center_weight;
};

constexpr uint32_t BIN_COUNT     = 256;
constexpr uint32_t BUFFER_SLOTS  = BIN_COUNT + 1;
constexpr uint32_t GROUP_SIZE    = 16;
constexpr uint32_t SAMPLE_STRIDE = 4;

constexpr float MIN_LOG_LUMINANCE   = -10.0f;
constexpr float LOG_LUMINANCE_RANGE = 30.0f;

constexpr float LOW_PERCENT  = 0.1f;
constexpr float HIGH_PERCENT = 0.9f;

constexpr float EXPOSURE_SCALE      = 1.2f;
constexpr float CALIBRATION_OFFSET  = 3.0f;

constexpr float KEY_OFFSET              = 1.03f;
constexpr float KEY_SCALE               = 2.0f;
constexpr float KEY_REFERENCE_LUMINANCE = 1000.0f;

constexpr float MIDDLE_GREY = 0.18f;
constexpr float PI          = 3.14159265359f;

constexpr float MIN_ILLUMINANCE = 0.000001f;

VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;
VkDescriptorPool m_pool = VK_NULL_HANDLE;
VkDescriptorSet m_set = VK_NULL_HANDLE;

VkSampler m_sampler = VK_NULL_HANDLE;

VkImageView m_written_view = VK_NULL_HANDLE;

const shader* m_shader = nullptr;

buffer m_histograms[MAX_FRAMES_IN_FLIGHT];
bool m_histogram_ready[MAX_FRAMES_IN_FLIGHT] = {};

float m_ev100    = 0.0f;
float m_exposure = 1.0f;

bool m_adapted      = false;
bool m_snap_pending = false;

float to_exposure(float ev100)
{
    return 1.0f / (EXPOSURE_SCALE * std::exp2(ev100));
}

float adaptation_key(float luminance)
{
    return KEY_OFFSET - KEY_SCALE / (KEY_SCALE + std::log10(luminance + 1.0f));
}

float night_darkening_ev(float log_luminance, float strength)
{
    const float luminance = std::exp2(log_luminance);

    return std::max(std::log2(adaptation_key(KEY_REFERENCE_LUMINANCE) / adaptation_key(luminance)), 0.0f) * strength;
}

float luminance_ev100(float log_luminance, float night_darkening)
{
    return log_luminance + CALIBRATION_OFFSET + night_darkening_ev(log_luminance, night_darkening);
}

bool incident_ev100(float illuminance, float night_darkening, float& ev100)
{
    if (!(illuminance > MIN_ILLUMINANCE)) {
        return false;
    }

    ev100 = luminance_ev100(std::log2(illuminance * MIDDLE_GREY / PI), night_darkening);

    return true;
}

float combine_metering(float incident, float histogram)
{
    return std::max(incident, histogram);
}

bool histogram_ev100(const std::array<uint32_t, BUFFER_SLOTS>& bins, float night_darkening, float& ev100)
{
    uint64_t total = 0;

    for (uint32_t i = 1; i < BIN_COUNT; i++) {
        total += bins[i];
    }

    if (total == 0) {
        return false;
    }

    const float low  = static_cast<float>(total) * LOW_PERCENT;
    const float high = static_cast<float>(total) * HIGH_PERCENT;

    float cumulative = 0.0f;
    float weight_sum = 0.0f;
    float log_sum    = 0.0f;

    for (uint32_t i = 1; i < BIN_COUNT; i++) {
        const float count = static_cast<float>(bins[i]);

        const float begin = std::max(cumulative, low);
        const float end   = std::min(cumulative + count, high);

        cumulative += count;

        if (end <= begin) {
            continue;
        }

        const float log_luminance = MIN_LOG_LUMINANCE + (static_cast<float>(i - 1) + 0.5f) / static_cast<float>(BIN_COUNT - 2) * LOG_LUMINANCE_RANGE;

        log_sum    += log_luminance * (end - begin);
        weight_sum += end - begin;
    }

    if (weight_sum <= 0.0f) {
        return false;
    }

    ev100 = luminance_ev100(log_sum / weight_sum, night_darkening);

    return true;
}

}

void init_all()
{
    m_set_layout = vk::descriptors::create_set_layout({
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    });

    m_pool = vk::descriptors::create_pool(1, { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 } });
    m_set  = vk::descriptors::allocate_set(m_pool, m_set_layout);

    sampler_create_info sampler_info;
    sampler_info.mag_filter = VK_FILTER_NEAREST;
    sampler_info.min_filter = VK_FILTER_NEAREST;
    sampler_info.mip_mode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.wrap_u     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_v     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_w     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropy = false;

    m_sampler = vk::samplers::get(vk::samplers::make(sampler_info));

    vk::descriptors::write_image(m_set, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, clouds::get_shadow_image().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_sampler);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_histograms[i] = vk::resources::create_buffer(
            sizeof(uint32_t) * BUFFER_SLOTS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
        );

        m_histogram_ready[i] = false;
    }

    compute_pipeline_create_info info;
    info.set_layouts        = { m_set_layout };
    info.push_constant_size = sizeof(histogram_push);

    m_shader = res::shaders::build("exposure_histogram", "", "", "sys/post/exposure_histogram.comp", {}, info, render_queue::opaque);
}

void clean_all()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::resources::destroy_buffer(m_histograms[i]);

        m_histogram_ready[i] = false;
    }

    vk::descriptors::destroy_pool(m_pool);
    vk::descriptors::destroy_set_layout(m_set_layout);

    m_set_layout = VK_NULL_HANDLE;
    m_pool       = VK_NULL_HANDLE;
    m_set        = VK_NULL_HANDLE;

    m_sampler      = VK_NULL_HANDLE;
    m_written_view = VK_NULL_HANDLE;

    m_shader = nullptr;

    m_adapted = false;
}

void handle_all()
{
    const VkImageView hdr_view = it::get_output_image().view;

    if (hdr_view != m_written_view) {
        vk::descriptors::write_image(m_set, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, hdr_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_sampler);

        m_written_view = hdr_view;
    }

    const gpu_post& post = frame_pipe::get_post();

    if (!post.auto_exposure) {
        m_ev100    = post.ev100;
        m_exposure = to_exposure(m_ev100);
        m_adapted  = false;
        return;
    }

    if (!m_adapted) {
        m_ev100        = post.ev100;
        m_adapted      = true;
        m_snap_pending = true;
    }

    const uint32_t frame = vk::frame::get_current_frame_index();

    if (m_histogram_ready[frame]) {
        std::array<uint32_t, BUFFER_SLOTS> bins = {};

        vk::resources::download_buffer(m_histograms[frame], bins.data(), sizeof(uint32_t) * BUFFER_SLOTS);

        float illuminance = 0.0f;
        std::memcpy(&illuminance, &bins[BIN_COUNT], sizeof(illuminance));

        float histogram = 0.0f;
        float incident  = 0.0f;

        const bool has_histogram = histogram_ev100(bins, post.exposure_night_darkening, histogram);
        const bool has_incident  = post.exposure_incident_metering && incident_ev100(illuminance, post.exposure_night_darkening, incident);

        float target = has_incident ? (has_histogram ? combine_metering(incident, histogram) : incident) : histogram;

        if (has_incident || has_histogram) {
            const float speed = target > m_ev100 ? post.exposure_speed_up : post.exposure_speed_down;

            m_ev100 += (target - m_ev100) * (1.0f - std::exp(-utilities::core::timer::delta * speed));

            if (m_snap_pending) {
                m_ev100        = target;
                m_snap_pending = false;
            }
        }
    }

    m_ev100    = std::clamp(m_ev100, post.min_ev100, post.max_ev100);
    m_exposure = to_exposure(m_ev100 - post.exposure_compensation);
}

void dispatch()
{
    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const uint32_t frame = vk::frame::get_current_frame_index();

    const buffer& histogram = m_histograms[frame];

    disp.cmdFillBuffer(cmd, histogram.handle, 0, histogram.size, 0);

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );

    const image& hdr = it::get_output_image();

    histogram_push push;
    push.histogram           = histogram.address;
    push.frame               = it::get_frame_data_address();
    push.inverse_exposure    = 1.0f / m_exposure;
    push.min_log_luminance   = MIN_LOG_LUMINANCE;
    push.log_luminance_range = LOG_LUMINANCE_RANGE;
    push.center_weight       = frame_pipe::get_post().exposure_center_weight;

    disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_shader->compute.layout, 0, 1, &m_set, 0, nullptr);

    vk::commands::dispatch(
        cmd,
        m_shader->compute,
        &push,
        sizeof(push),
        (hdr.extent.width / SAMPLE_STRIDE + GROUP_SIZE - 1) / GROUP_SIZE,
        (hdr.extent.height / SAMPLE_STRIDE + GROUP_SIZE - 1) / GROUP_SIZE,
        1
    );

    vk::commands::memory_barrier(
        cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_HOST_BIT,
        VK_ACCESS_2_HOST_READ_BIT
    );

    m_histogram_ready[frame] = true;
}

float get_exposure() { return m_exposure; }

}
