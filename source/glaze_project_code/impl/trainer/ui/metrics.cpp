#include "glp/trainer/ui/metrics.h"

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include "glp/trainer/ui/panels.h"

#include "glp/glaze/rendering/scene_render/shadows.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/profiler.h"
#include "glp/glaze/rendering/vk/resources.h"

#include "glp/glaze/utilities/core/timer.h"
#include "glp/glaze/utilities/system_load.h"

#include <imgui.h>

#include <vk_mem_alloc.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace trainer::ui::metrics
{

namespace
{

namespace system_load = glaze::utilities::system_load;

constexpr int FRAME_HISTORY_SIZE = 240;
constexpr int LOAD_HISTORY_SIZE  = 120;

constexpr float SAMPLE_INTERVAL = 0.5f;

constexpr float GRAPH_HEIGHT      = 60.0f;
constexpr float LOAD_GRAPH_HEIGHT = 40.0f;
constexpr float GRAPH_HEADROOM    = 1.25f;

constexpr float SUMMARY_COLUMN_WIDTH = 0.25f;

constexpr float LOADS_LABEL_WIDTH = 0.13f;
constexpr float LOADS_BAR_WIDTH   = 0.87f;

constexpr float PASSES_NAME_WIDTH  = 0.16f;
constexpr float PASSES_MS_WIDTH    = 0.08f;
constexpr float PASSES_SHARE_WIDTH = 0.76f;

constexpr float MEMORY_NAME_WIDTH = 0.26f;
constexpr float MEMORY_SIZE_WIDTH = 0.13f;
constexpr float MEMORY_LOAD_WIDTH = 0.61f;

constexpr float SHADOWS_NAME_WIDTH  = 0.5f;
constexpr float SHADOWS_VALUE_WIDTH = 0.5f;

constexpr float WARN_LOAD = 0.75f;
constexpr float BAD_LOAD  = 0.95f;

constexpr float MINIMIZED_EDGE_SPACE_X = 0.0039f;
constexpr float MINIMIZED_MARGIN_Y = 0.02f;
constexpr float MINIMIZED_BG_ALPHA = 0.7f;
constexpr float MINIMIZED_SCALE    = 0.985f;

constexpr float PERCENT = 100.0f;

constexpr float SECONDS_TO_MS = 1000.0f;

constexpr double BYTES_TO_MB = 1.0 / (1024.0 * 1024.0);

const ImVec2 MASTER_WINDOW_SIZE = ImVec2(497.0f, 459.0f);

const ImVec4 GOOD_COLOR = ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 WARN_COLOR = ImVec4(0.95f, 0.80f, 0.30f, 1.0f);
const ImVec4 BAD_COLOR  = ImVec4(0.95f, 0.40f, 0.35f, 1.0f);
const ImVec4 NONE_COLOR = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);

std::array<float, FRAME_HISTORY_SIZE> m_cpu_frame_history = {};
std::array<float, FRAME_HISTORY_SIZE> m_gpu_frame_history = {};

std::array<float, LOAD_HISTORY_SIZE> m_cpu_load_history = {};
std::array<float, LOAD_HISTORY_SIZE> m_gpu_load_history = {};

int m_frame_history_offset = 0;
int m_load_history_offset  = 0;

float m_sample_elapsed_ms = 0.0f;
int m_sample_frames       = 0;

float m_fps      = 0.0f;
float m_delta_ms = 0.0f;

bool m_is_load_initialized = false;

ImVec2 m_minimized_position = ImVec2(0.0f, 0.0f);
ImVec2 m_minimized_size     = ImVec2(0.0f, 0.0f);

bool m_minimized_visible = false;

bool m_master_open    = false;
bool m_minimized_open = true;

void init_load()
{
    const VkPhysicalDeviceProperties& properties     = glaze::rendering::vk::device::get_physical().properties;
    const VkPhysicalDeviceIDProperties& id_properties = glaze::rendering::vk::device::get_id_properties();

    system_load::gpu_identity gpu;

    gpu.vendor_id = properties.vendorID;
    gpu.device_id = properties.deviceID;

    std::memcpy(gpu.uuid, id_properties.deviceUUID, sizeof(gpu.uuid));
    std::memcpy(gpu.luid, id_properties.deviceLUID, sizeof(gpu.luid));

    gpu.luid_valid = id_properties.deviceLUIDValid == VK_TRUE;

    system_load::init(gpu);

    m_is_load_initialized = true;
}

void push_load_history()
{
    m_cpu_load_history[m_load_history_offset] = std::max(system_load::get_cpu_load(), 0.0f) * PERCENT;
    m_gpu_load_history[m_load_history_offset] = std::max(system_load::get_gpu_load(), 0.0f) * PERCENT;

    m_load_history_offset = (m_load_history_offset + 1) % LOAD_HISTORY_SIZE;
}

ImVec4 load_color(float load)
{
    if (load < 0.0f) {
        return NONE_COLOR;
    }

    if (load >= BAD_LOAD) {
        return BAD_COLOR;
    }

    return load >= WARN_LOAD ? WARN_COLOR : GOOD_COLOR;
}

void format_load(char* text, size_t size, float load)
{
    if (load < 0.0f) {
        std::snprintf(text, size, "n/a");
        return;
    }

    std::snprintf(text, size, "%.0f%%", load * PERCENT);
}

void draw_load_text(const char* label, float load)
{
    char value[16];
    format_load(value, sizeof(value), load);

    ImGui::TextDisabled("%s", label);
    ImGui::SameLine();
    ImGui::TextColored(load_color(load), "%s", value);
}

void draw_load_bar(const char* label, float load)
{
    char overlay[16];
    format_load(overlay, sizeof(overlay), load);

    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    ImGui::TableSetColumnIndex(1);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, load_color(load));
    ImGui::ProgressBar(std::max(load, 0.0f), ImVec2(-1.0f, 0.0f), overlay);
    ImGui::PopStyleColor();
}

void draw_summary_cell(const char* label, const ImVec4& color, const char* value)
{
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", label);
    ImGui::TextColored(color, "%s", value);
}

void draw_summary()
{
    const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame;

    if (!ImGui::BeginTable("summary", 4, flags)) {
        return;
    }

    for (int i = 0; i < 4; i++) {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, SUMMARY_COLUMN_WIDTH);
    }

    char fps[16];
    char delta[16];
    char cpu[16];
    char gpu[16];

    std::snprintf(fps, sizeof(fps), "%.0f", m_fps);
    std::snprintf(delta, sizeof(delta), "%.2f ms", m_delta_ms);

    format_load(cpu, sizeof(cpu), system_load::get_cpu_load());
    format_load(gpu, sizeof(gpu), system_load::get_gpu_load());

    const ImVec4 text_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    draw_summary_cell("FPS", text_color, fps);
    draw_summary_cell("dt", text_color, delta);
    draw_summary_cell("CPU", load_color(system_load::get_cpu_load()), cpu);
    draw_summary_cell("GPU", load_color(system_load::get_gpu_load()), gpu);

    ImGui::EndTable();
}

void draw_graph(const char* id, const float* values, int count, int offset, const char* overlay, float max_value, float height)
{
    ImGui::PlotLines(id, values, count, offset, overlay, 0.0f, max_value, ImVec2(-1.0f, height));
}

void draw_frame_graph(const char* title, const std::array<float, FRAME_HISTORY_SIZE>& history)
{
    const float peak = *std::max_element(history.begin(), history.end());

    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%.2f ms", history[(m_frame_history_offset + FRAME_HISTORY_SIZE - 1) % FRAME_HISTORY_SIZE]);

    ImGui::TextDisabled("%s", title);

    ImGui::PushID(title);
    draw_graph("##graph", history.data(), FRAME_HISTORY_SIZE, m_frame_history_offset, overlay, std::max(peak, 1.0f) * GRAPH_HEADROOM, GRAPH_HEIGHT);
    ImGui::PopID();
}

void draw_load_graph(const char* title, const std::array<float, LOAD_HISTORY_SIZE>& history, float load)
{
    char overlay[16];
    format_load(overlay, sizeof(overlay), load);

    ImGui::TextDisabled("%s", title);

    ImGui::PushID(title);
    ImGui::PushStyleColor(ImGuiCol_PlotLines, load_color(load));
    draw_graph("##graph", history.data(), LOAD_HISTORY_SIZE, m_load_history_offset, overlay, PERCENT, LOAD_GRAPH_HEIGHT);
    ImGui::PopStyleColor();
    ImGui::PopID();
}

void draw_overview_tab()
{
    if (ImGui::BeginTable("loads", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, LOADS_LABEL_WIDTH);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, LOADS_BAR_WIDTH);

        draw_load_bar("CPU load", system_load::get_cpu_load());
        draw_load_bar("GPU load", system_load::get_gpu_load());

        ImGui::EndTable();
    }

    ImGui::Spacing();

    draw_load_graph("cpu load", m_cpu_load_history, system_load::get_cpu_load());
    draw_load_graph("gpu load", m_gpu_load_history, system_load::get_gpu_load());

    ImGui::SeparatorText("frame time");

    draw_frame_graph("cpu frame", m_cpu_frame_history);
    draw_frame_graph("gpu frame", m_gpu_frame_history);
}

void draw_passes_tab()
{
    const glaze::rendering::gpu_timings& timings = glaze::rendering::vk::profiler::get_timings();

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("passes", 3, flags)) {
        return;
    }

    ImGui::TableSetupColumn("pass", ImGuiTableColumnFlags_WidthStretch, PASSES_NAME_WIDTH);
    ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthStretch, PASSES_MS_WIDTH);
    ImGui::TableSetupColumn("share", ImGuiTableColumnFlags_WidthStretch, PASSES_SHARE_WIDTH);
    ImGui::TableHeadersRow();

    for (uint32_t i = 0; i < timings.count; i++) {
        const float share = timings.total_ms > 0.0f ? timings.ms[i] / timings.total_ms : 0.0f;

        char overlay[16];
        std::snprintf(overlay, sizeof(overlay), "%.0f%%", share * PERCENT);

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(timings.names[i]);

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.2f", timings.ms[i]);

        ImGui::TableSetColumnIndex(2);
        ImGui::ProgressBar(share, ImVec2(-1.0f, 0.0f), overlay);
    }

    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("total");

    ImGui::TableSetColumnIndex(1);
    ImGui::TextDisabled("%.2f", timings.total_ms);

    ImGui::EndTable();
}

void draw_memory_row(const char* label, uint64_t size, uint64_t usage, uint64_t limit)
{
    const double used      = static_cast<double>(usage) * BYTES_TO_MB;
    const double available = static_cast<double>(limit) * BYTES_TO_MB;

    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%.0f / %.0f MB", used, available);

    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.0f MB", static_cast<double>(size) * BYTES_TO_MB);

    ImGui::TableSetColumnIndex(2);
    ImGui::ProgressBar(available > 0.0 ? static_cast<float>(used / available) : 0.0f, ImVec2(-1.0f, 0.0f), overlay);
}

void draw_memory_tab()
{
    const VmaAllocator allocator = glaze::rendering::vk::resources::get_allocator();

    const VkPhysicalDeviceMemoryProperties* properties = nullptr;

    vmaGetMemoryProperties(allocator, &properties);

    std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets = {};

    vmaGetHeapBudgets(allocator, budgets.data());

    const bool is_integrated = glaze::rendering::vk::device::get_physical().properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;

    uint32_t main_heap = VK_MAX_MEMORY_HEAPS;

    for (uint32_t heap = 0; heap < properties->memoryHeapCount; heap++) {
        if ((properties->memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) {
            continue;
        }

        if (main_heap == VK_MAX_MEMORY_HEAPS || properties->memoryHeaps[heap].size > properties->memoryHeaps[main_heap].size) {
            main_heap = heap;
        }
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("memory", 3, flags)) {
        return;
    }

    ImGui::TableSetupColumn("memory", ImGuiTableColumnFlags_WidthStretch, MEMORY_NAME_WIDTH);
    ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthStretch, MEMORY_SIZE_WIDTH);
    ImGui::TableSetupColumn("load", ImGuiTableColumnFlags_WidthStretch, MEMORY_LOAD_WIDTH);
    ImGui::TableHeadersRow();

    const system_load::memory_usage& ram  = system_load::get_ram_usage();
    const system_load::memory_usage& swap = system_load::get_swap_usage();

    if (ram.available) {
        draw_memory_row("RAM", ram.total, ram.used, ram.total);
    }

    if (swap.available) {
        draw_memory_row("RAM: Swap", swap.total, swap.used, swap.total);
    }

    if (main_heap == VK_MAX_MEMORY_HEAPS) {
        ImGui::EndTable();
        return;
    }

    const VkMemoryPropertyFlags cpu_visible_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

    bool main_heap_cpu_visible = false;

    VkDeviceSize main_heap_cpu_visible_bytes = 0;

    VmaTotalStatistics statistics = {};

    vmaCalculateStatistics(allocator, &statistics);

    for (uint32_t type = 0; type < properties->memoryTypeCount; type++) {
        if (properties->memoryTypes[type].heapIndex != main_heap || (properties->memoryTypes[type].propertyFlags & cpu_visible_flags) != cpu_visible_flags) {
            continue;
        }

        main_heap_cpu_visible = true;

        main_heap_cpu_visible_bytes += statistics.memoryType[type].statistics.blockBytes;
    }

    draw_memory_row(
        is_integrated ? "VRAM (shared)" : "VRAM",
        properties->memoryHeaps[main_heap].size,
        budgets[main_heap].usage,
        budgets[main_heap].budget
    );

    if (main_heap_cpu_visible) {
        draw_memory_row(
            is_integrated ? "VRAM: CPU-visible (shared)" : "VRAM: CPU-visible",
            properties->memoryHeaps[main_heap].size,
            main_heap_cpu_visible_bytes,
            budgets[main_heap].budget
        );
    }

    for (uint32_t heap = 0; heap < properties->memoryHeapCount; heap++) {
        if (heap == main_heap || (properties->memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) {
            continue;
        }

        draw_memory_row("VRAM CPU-visible", properties->memoryHeaps[heap].size, budgets[heap].usage, budgets[heap].budget);
    }

    ImGui::EndTable();
}

void draw_stat_row(const char* name, uint32_t value)
{
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(name);

    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%u", value);
}

void draw_shadows_tab()
{
    namespace shadows = glaze::rendering::scene_render::shadows;

    if (!shadows::is_active()) {
        ImGui::TextDisabled("no shadowed light");
    }

    const shadows::shadow_stats& stats = shadows::get_stats();

    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%u / %u pages", stats.pool_pages - stats.free_pages, stats.pool_pages);

    ImGui::ProgressBar(stats.pool_pages > 0 ? static_cast<float>(stats.pool_pages - stats.free_pages) / static_cast<float>(stats.pool_pages) : 0.0f, ImVec2(-1.0f, 0.0f), overlay);

    if (ImGui::BeginTable("shadow_stats", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, SHADOWS_NAME_WIDTH);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, SHADOWS_VALUE_WIDTH);

        draw_stat_row("requested", stats.requested_pages);
        draw_stat_row("rendered", stats.rendered_pages);
        draw_stat_row("failed", stats.failed_pages);
        draw_stat_row("opaque draws", stats.opaque_draws);
        draw_stat_row("masked draws", stats.masked_draws);
        draw_stat_row("casters", stats.casters);
        draw_stat_row("invalidations", stats.invalidations);

        ImGui::EndTable();
    }
}

void draw_master()
{
    ImGui::SetNextWindowSize(MASTER_WINDOW_SIZE, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(1.0f);

    if (!ImGui::Begin("Metrics Master", &m_master_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    draw_summary();

    ImGui::Spacing();

    if (ImGui::BeginTabBar("metrics_tabs")) {
        if (ImGui::BeginTabItem("Overview")) {
            draw_overview_tab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("GPU Passes")) {
            draw_passes_tab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Memory Load")) {
            draw_memory_tab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Shadows")) {
            draw_shadows_tab();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void draw_minimized()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    const ImGuiStyle& style = ImGui::GetStyle();

    const float padding    = style.WindowPadding.x * MINIMIZED_SCALE;
    const float edge_space = viewport->WorkSize.x * MINIMIZED_EDGE_SPACE_X;

    const ImVec2 position(
        viewport->WorkPos.x + viewport->WorkSize.x + std::max(padding - edge_space, 0.0f),
        viewport->WorkPos.y + viewport->WorkSize.y * MINIMIZED_MARGIN_Y
    );

    ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(MINIMIZED_BG_ALPHA);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoInputs;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x * MINIMIZED_SCALE, style.WindowPadding.y * MINIMIZED_SCALE));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x * MINIMIZED_SCALE, style.CellPadding.y * MINIMIZED_SCALE));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x * MINIMIZED_SCALE, style.ItemSpacing.y * MINIMIZED_SCALE));

    if (!ImGui::Begin("##metrics_minimized", nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleVar(3);
        return;
    }

    if (ImGui::BeginTable("minimized", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX)) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("FPS");
        ImGui::SameLine();
        ImGui::Text("%.0f", m_fps);

        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("dt");
        ImGui::SameLine();
        ImGui::Text("%.2f ms", m_delta_ms);

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        draw_load_text("CPU", system_load::get_cpu_load());

        ImGui::TableSetColumnIndex(1);
        draw_load_text("GPU", system_load::get_gpu_load());

        ImGui::EndTable();
    }

    const float extra_space = edge_space - padding - ImGui::GetStyle().ItemSpacing.x;

    if (extra_space > 0.0f) {
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(extra_space, 0.0f));
    }

    m_minimized_position = ImGui::GetWindowPos();
    m_minimized_size     = ImGui::GetWindowSize();
    m_minimized_visible  = true;

    ImGui::End();

    ImGui::PopStyleVar(3);
}

const bool registered_minimized = [] {
    panels::register_panel({ "Metrics", "Show Minimized", &m_minimized_open, draw_minimized, 1 });
    return true;
}();

const bool registered_master = [] {
    panels::register_panel({ "Metrics", "Metrics Master", &m_master_open, draw_master, 2 });
    return true;
}();

}

void update()
{
    m_minimized_visible = false;

    if (!m_is_load_initialized) {
        init_load();
    }

    m_cpu_frame_history[m_frame_history_offset] = glaze::utilities::core::timer::delta_ms;
    m_gpu_frame_history[m_frame_history_offset] = glaze::rendering::vk::profiler::get_timings().total_ms;

    m_frame_history_offset = (m_frame_history_offset + 1) % FRAME_HISTORY_SIZE;

    m_sample_elapsed_ms += glaze::utilities::core::timer::delta_ms;
    m_sample_frames++;

    if (m_sample_elapsed_ms < SAMPLE_INTERVAL * SECONDS_TO_MS) {
        return;
    }

    m_fps      = static_cast<float>(m_sample_frames) * SECONDS_TO_MS / m_sample_elapsed_ms;
    m_delta_ms = m_sample_elapsed_ms / static_cast<float>(m_sample_frames);

    m_sample_elapsed_ms = 0.0f;
    m_sample_frames     = 0;

    system_load::sample();

    push_load_history();
}

void cleanup()
{
    if (m_is_load_initialized) {
        system_load::cleanup();
    }

    m_is_load_initialized = false;
}

bool get_minimized_bounds(ImVec2& position, ImVec2& size)
{
    position = m_minimized_position;
    size     = m_minimized_size;

    return m_minimized_visible;
}

}

#endif
