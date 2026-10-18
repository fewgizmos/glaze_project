#include "glp/glaze/rendering/vk/profiler.h"

#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"

#include "glp/glaze/rendering/defines.h"

#include "glp/glaze/utilities/core/assert.h"
#include "glp/glaze/utilities/core/timer.h"

#include <array>
#include <cstdint>

namespace glaze::rendering::vk::profiler
{

namespace
{

constexpr float AVERAGE_INTERVAL   = 2.0f;
constexpr double NANOSECONDS_TO_MS = 0.000001;

VkQueryPool m_pools[MAX_FRAMES_IN_FLIGHT] = {};

std::array<const char*, PROFILER_MAX_MARKS> m_names[MAX_FRAMES_IN_FLIGHT] = {};
uint32_t m_counts[MAX_FRAMES_IN_FLIGHT] = {};

std::array<const char*, PROFILER_MAX_MARKS> m_total_names = {};
std::array<double, PROFILER_MAX_MARKS> m_totals = {};

uint32_t m_total_count  = 0;
uint32_t m_total_frames = 0;

float m_last_average = 0.0f;

gpu_timings m_timings;

void collect(uint32_t frame)
{
    const uint32_t count = m_counts[frame];

    if (count < 2) {
        return;
    }

    std::array<uint64_t, PROFILER_MAX_MARKS> stamps = {};

    const VkResult result = device::get_disp().getQueryPoolResults(
        m_pools[frame],
        0,
        count,
        sizeof(uint64_t) * count,
        stamps.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT
    );

    if (result != VK_SUCCESS) {
        return;
    }

    const double period = device::get_physical().properties.limits.timestampPeriod;

    for (uint32_t i = 1; i < count; i++) {
        m_totals[i]      += static_cast<double>(stamps[i] - stamps[i - 1]) * period * NANOSECONDS_TO_MS;
        m_total_names[i]  = m_names[frame][i];
    }

    m_totals[0] += static_cast<double>(stamps[count - 1] - stamps[0]) * period * NANOSECONDS_TO_MS;

    m_total_count = count;
    m_total_frames++;
}

void average()
{
    if (m_total_frames == 0) {
        return;
    }

    m_timings.count = 0;

    for (uint32_t i = 1; i < m_total_count; i++) {
        m_timings.names[m_timings.count]        = m_total_names[i];
        m_timings.milliseconds[m_timings.count] = static_cast<float>(m_totals[i] / m_total_frames);
        m_timings.count++;
    }

    m_timings.total_milliseconds = static_cast<float>(m_totals[0] / m_total_frames);

    m_totals       = {};
    m_total_frames = 0;
}

}

void create()
{
    VkQueryPoolCreateInfo create_info = {};
    create_info.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    create_info.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    create_info.queryCount = PROFILER_MAX_MARKS;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        const VkResult result = device::get_disp().createQueryPool(&create_info, nullptr, &m_pools[i]);

        GLAZE_ASSERT(result != VK_SUCCESS, "failed to create timestamp query pool");

        m_counts[i] = 0;
    }
}

void destroy()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        device::get_disp().destroyQueryPool(m_pools[i], nullptr);

        m_pools[i] = VK_NULL_HANDLE;
    }
}

void begin_frame(VkCommandBuffer cmd)
{
    const uint32_t frame = vk::frame::get_current_frame_index();

    collect(frame);

    if (utilities::core::timer::timestamp - m_last_average >= AVERAGE_INTERVAL) {
        average();

        m_last_average = utilities::core::timer::timestamp;
    }

    device::get_disp().cmdResetQueryPool(cmd, m_pools[frame], 0, PROFILER_MAX_MARKS);

    m_counts[frame] = 0;

    mark(cmd, "start");
}

void mark(VkCommandBuffer cmd, const char* name)
{
    const uint32_t frame = vk::frame::get_current_frame_index();

    if (m_counts[frame] >= PROFILER_MAX_MARKS) {
        return;
    }

    device::get_disp().cmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_pools[frame], m_counts[frame]);

    m_names[frame][m_counts[frame]] = name;
    m_counts[frame]++;
}

const gpu_timings& get_timings() { return m_timings; }

}
