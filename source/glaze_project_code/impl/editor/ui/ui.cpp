#ifndef NDEBUG

#include "glp/editor/ui/ui.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"
#include "glp/glaze/rendering/vk/profiler.h"
#include "glp/glaze/rendering/vk/resources.h"

#include "glp/glaze/utilities/core/timer.h"

#include <imgui.h>

#include <vk_mem_alloc.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

namespace editor::ui::it
{

namespace
{

constexpr int HISTORY_SIZE = 240;

constexpr float GRAPH_HEIGHT   = 60.0f;
constexpr float GRAPH_HEADROOM = 1.25f;

constexpr double BYTES_TO_MEGABYTES = 1.0 / (1024.0 * 1024.0);

const ImVec2 METRICS_WINDOW_SIZE = ImVec2(396.0f, 306.0f);

std::array<float, HISTORY_SIZE> m_cpu_history = {};
std::array<float, HISTORY_SIZE> m_gpu_history = {};

int m_history_offset = 0;

bool m_metrics_open = true;

void record_history()
{
    m_cpu_history[m_history_offset] = glaze::utilities::core::timer::delta_ms;
    m_gpu_history[m_history_offset] = glaze::rendering::vk::profiler::get_timings().total_milliseconds;

    m_history_offset = (m_history_offset + 1) % HISTORY_SIZE;
}

void draw_graph(const char* label, const std::array<float, HISTORY_SIZE>& history)
{
    const float peak = *std::max_element(history.begin(), history.end());

    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%.2f ms", history[(m_history_offset + HISTORY_SIZE - 1) % HISTORY_SIZE]);

    ImGui::PlotLines(label, history.data(), HISTORY_SIZE, m_history_offset, overlay, 0.0f, std::max(peak, 1.0f) * GRAPH_HEADROOM, ImVec2(0.0f, GRAPH_HEIGHT));
}

void draw_passes()
{
    const glaze::rendering::gpu_timings& timings = glaze::rendering::vk::profiler::get_timings();

    if (!ImGui::BeginTable("passes", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        return;
    }

    for (uint32_t i = 0; i < timings.count; i++) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(timings.names[i]);

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.2f ms", timings.milliseconds[i]);
    }

    ImGui::EndTable();
}

void draw_video_memory()
{
    const VmaAllocator allocator = glaze::rendering::vk::resources::get_allocator();

    const VkPhysicalDeviceMemoryProperties* properties = nullptr;

    vmaGetMemoryProperties(allocator, &properties);

    std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets = {};

    vmaGetHeapBudgets(allocator, budgets.data());

    for (uint32_t heap = 0; heap < properties->memoryHeapCount; heap++) {
        if ((properties->memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) {
            continue;
        }

        bool host_visible = false;

        for (uint32_t type = 0; type < properties->memoryTypeCount; type++) {
            if (properties->memoryTypes[type].heapIndex == heap && (properties->memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
                host_visible = true;
            }
        }

        const double heap_size = static_cast<double>(properties->memoryHeaps[heap].size) * BYTES_TO_MEGABYTES;

        ImGui::Text("%s  %.0f MB", host_visible ? "VRAM (CPU-visible)" : "VRAM", heap_size);

        const double used      = static_cast<double>(budgets[heap].usage) * BYTES_TO_MEGABYTES;
        const double available = static_cast<double>(budgets[heap].budget) * BYTES_TO_MEGABYTES;

        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "%.0f / %.0f MB", used, available);

        ImGui::ProgressBar(available > 0.0 ? static_cast<float>(used / available) : 0.0f, ImVec2(-1.0f, 0.0f), overlay);
    }
}

void draw_metrics()
{
    ImGui::SetNextWindowSize(METRICS_WINDOW_SIZE, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(1.0f);

    if (!ImGui::Begin("Metrics", &m_metrics_open)) {
        ImGui::End();
        return;
    }

    ImGui::Text("%.0f fps", glaze::utilities::core::timer::fps);

    draw_graph("cpu frame", m_cpu_history);
    draw_graph("gpu frame", m_gpu_history);

    if (ImGui::CollapsingHeader("gpu passes")) {
        draw_passes();
    }

    ImGui::SeparatorText("video memory");

    draw_video_memory();

    ImGui::End();
}

}

void draw()
{
    record_history();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            if (ImGui::BeginMenu("Editor")) {
                ImGui::MenuItem("Metrics", nullptr, &m_metrics_open);
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Actions")) {
            if (ImGui::BeginMenu("Editor")) {
                if (ImGui::MenuItem("Reload Shaders")) {
                    glaze::rendering::scene_render::res::shaders::reload_all();
                }

                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    if (m_metrics_open) {
        draw_metrics();
    }
}

}

#endif
