#include "glp/glaze/utilities/system_load.h"

#include <algorithm>
#include <cstdio>
#include <string>

#ifdef _WIN32

#include <windows.h>
#include <pdh.h>
#include <psapi.h>

#include <cstring>
#include <cwchar>
#include <map>
#include <vector>

#else

#include <dlfcn.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>

#endif

namespace glaze::utilities::system_load
{

namespace
{

constexpr float UNAVAILABLE = -1.0f;

constexpr float PERCENT_TO_LOAD = 0.01f;

float m_cpu_load = UNAVAILABLE;
float m_gpu_load = UNAVAILABLE;

uint64_t m_last_cpu_idle  = 0;
uint64_t m_last_cpu_total = 0;

memory_usage m_ram;
memory_usage m_swap;

bool m_is_initialized = false;

void apply_cpu_times(uint64_t idle, uint64_t total)
{
    const uint64_t idle_delta  = idle - m_last_cpu_idle;
    const uint64_t total_delta = total - m_last_cpu_total;

    if (m_last_cpu_total != 0 && total_delta > 0) {
        m_cpu_load = std::clamp(1.0f - static_cast<float>(idle_delta) / static_cast<float>(total_delta), 0.0f, 1.0f);
    }

    m_last_cpu_idle  = idle;
    m_last_cpu_total = total;
}

#ifdef _WIN32

PDH_HQUERY m_gpu_query     = nullptr;
PDH_HCOUNTER m_gpu_counter = nullptr;

std::wstring m_luid_tag;

uint64_t to_uint64(const FILETIME& time)
{
    return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

void sample_cpu()
{
    FILETIME idle   = {};
    FILETIME kernel = {};
    FILETIME user   = {};

    if (!GetSystemTimes(&idle, &kernel, &user)) {
        return;
    }

    apply_cpu_times(to_uint64(idle), to_uint64(kernel) + to_uint64(user));
}

void sample_memory()
{
    MEMORYSTATUSEX status = {};
    status.dwLength = sizeof(status);

    PROCESS_MEMORY_COUNTERS counters = {};

    if (!GlobalMemoryStatusEx(&status) || !GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return;
    }

    m_ram.used      = counters.WorkingSetSize;
    m_ram.total     = status.ullTotalPhys;
    m_ram.available = true;
}

void init_gpu(const gpu_identity& gpu)
{
    if (!gpu.luid_valid) {
        return;
    }

    LUID luid = {};
    std::memcpy(&luid, gpu.luid, sizeof(luid));

    wchar_t tag[64];
    std::swprintf(tag, 64, L"luid_0x%08X_0x%08X", static_cast<unsigned int>(luid.HighPart), static_cast<unsigned int>(luid.LowPart));

    m_luid_tag = tag;

    if (PdhOpenQueryW(nullptr, 0, &m_gpu_query) != ERROR_SUCCESS) {
        m_gpu_query = nullptr;
        return;
    }

    if (PdhAddEnglishCounterW(m_gpu_query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &m_gpu_counter) != ERROR_SUCCESS) {
        PdhCloseQuery(m_gpu_query);

        m_gpu_query   = nullptr;
        m_gpu_counter = nullptr;

        return;
    }

    PdhCollectQueryData(m_gpu_query);
}

void cleanup_gpu()
{
    if (m_gpu_query != nullptr) {
        PdhCloseQuery(m_gpu_query);
    }

    m_gpu_query   = nullptr;
    m_gpu_counter = nullptr;

    m_luid_tag.clear();
}

void sample_gpu()
{
    if (m_gpu_query == nullptr || PdhCollectQueryData(m_gpu_query) != ERROR_SUCCESS) {
        return;
    }

    DWORD buffer_size = 0;
    DWORD item_count  = 0;

    if (PdhGetFormattedCounterArrayW(m_gpu_counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, nullptr) != static_cast<PDH_STATUS>(PDH_MORE_DATA)) {
        return;
    }

    std::vector<unsigned char> buffer(buffer_size);

    PDH_FMT_COUNTERVALUE_ITEM_W* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());

    if (PdhGetFormattedCounterArrayW(m_gpu_counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, items) != ERROR_SUCCESS) {
        return;
    }

    std::map<std::wstring, double> engines;

    for (DWORD i = 0; i < item_count; i++) {
        const std::wstring name = items[i].szName;

        if (name.find(m_luid_tag) == std::wstring::npos || items[i].FmtValue.CStatus != ERROR_SUCCESS) {
            continue;
        }

        const size_t engine_start = name.find(L"phys_");

        if (engine_start == std::wstring::npos) {
            continue;
        }

        engines[name.substr(engine_start)] += items[i].FmtValue.doubleValue;
    }

    double busiest = 0.0;

    for (const auto& [engine, percent] : engines) {
        busiest = std::max(busiest, percent);
    }

    m_gpu_load = std::clamp(static_cast<float>(busiest) * PERCENT_TO_LOAD, 0.0f, 1.0f);
}

#else

constexpr uint32_t NVIDIA_VENDOR_ID = 0x10DE;
constexpr uint32_t AMD_VENDOR_ID    = 0x1002;

constexpr int NVML_SUCCESS = 0;

constexpr unsigned int NVML_UUID_BUFFER_SIZE = 96;

constexpr int PROC_STAT_IDLE_FIELD   = 3;
constexpr int PROC_STAT_IOWAIT_FIELD = 4;
constexpr int PROC_STAT_TIME_FIELDS  = 8;

struct nvml_utilization
{
    unsigned int gpu;
    unsigned int memory;
};

using nvml_init_fn          = int (*)();
using nvml_shutdown_fn      = int (*)();
using nvml_count_fn         = int (*)(unsigned int*);
using nvml_handle_fn        = int (*)(unsigned int, void**);
using nvml_uuid_fn          = int (*)(void*, char*, unsigned int);
using nvml_utilization_fn   = int (*)(void*, nvml_utilization*);

void* m_nvml_library = nullptr;
void* m_nvml_device  = nullptr;

nvml_shutdown_fn m_nvml_shutdown          = nullptr;
nvml_utilization_fn m_nvml_utilization    = nullptr;

std::string m_sysfs_busy_path;

void sample_cpu()
{
    std::ifstream file("/proc/stat");

    std::string label;
    file >> label;

    if (label != "cpu") {
        return;
    }

    uint64_t idle  = 0;
    uint64_t total = 0;

    for (int field = 0; field < PROC_STAT_TIME_FIELDS; field++) {
        uint64_t value = 0;

        if (!(file >> value)) {
            return;
        }

        total += value;

        if (field == PROC_STAT_IDLE_FIELD || field == PROC_STAT_IOWAIT_FIELD) {
            idle += value;
        }
    }

    apply_cpu_times(idle, total);
}

constexpr uint64_t KB_TO_BYTES = 1024;

uint64_t read_kb_field(const char* path, const std::string& field, bool& found)
{
    std::ifstream file(path);

    std::string name;

    while (file >> name) {
        uint64_t value = 0;

        if (name == field && file >> value) {
            found = true;

            return value * KB_TO_BYTES;
        }

        file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }

    return 0;
}

void sample_memory()
{
    bool has_ram_used   = false;
    bool has_ram_total  = false;
    bool has_swap_used  = false;
    bool has_swap_total = false;

    m_ram.used  = read_kb_field("/proc/self/status", "VmRSS:", has_ram_used);
    m_ram.total = read_kb_field("/proc/meminfo", "MemTotal:", has_ram_total);

    m_ram.available = has_ram_used && has_ram_total;

    m_swap.used  = read_kb_field("/proc/self/status", "VmSwap:", has_swap_used);
    m_swap.total = read_kb_field("/proc/meminfo", "SwapTotal:", has_swap_total);

    m_swap.available = has_swap_used && has_swap_total && m_swap.total > 0;
}

std::string format_nvml_uuid(const uint8_t* uuid)
{
    char text[64];

    std::snprintf(
        text,
        sizeof(text),
        "GPU-%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
        uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]
    );

    return text;
}

std::string to_lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char symbol) { return static_cast<char>(std::tolower(symbol)); });

    return text;
}

void init_nvml(const gpu_identity& gpu)
{
    m_nvml_library = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);

    if (m_nvml_library == nullptr) {
        return;
    }

    const auto init_nvml_fn = reinterpret_cast<nvml_init_fn>(dlsym(m_nvml_library, "nvmlInit_v2"));
    const auto count_fn     = reinterpret_cast<nvml_count_fn>(dlsym(m_nvml_library, "nvmlDeviceGetCount_v2"));
    const auto handle_fn    = reinterpret_cast<nvml_handle_fn>(dlsym(m_nvml_library, "nvmlDeviceGetHandleByIndex_v2"));
    const auto uuid_fn      = reinterpret_cast<nvml_uuid_fn>(dlsym(m_nvml_library, "nvmlDeviceGetUUID"));

    m_nvml_shutdown    = reinterpret_cast<nvml_shutdown_fn>(dlsym(m_nvml_library, "nvmlShutdown"));
    m_nvml_utilization = reinterpret_cast<nvml_utilization_fn>(dlsym(m_nvml_library, "nvmlDeviceGetUtilizationRates"));

    if (init_nvml_fn == nullptr || count_fn == nullptr || handle_fn == nullptr || uuid_fn == nullptr || m_nvml_shutdown == nullptr || m_nvml_utilization == nullptr || init_nvml_fn() != NVML_SUCCESS) {
        dlclose(m_nvml_library);

        m_nvml_library     = nullptr;
        m_nvml_shutdown    = nullptr;
        m_nvml_utilization = nullptr;

        return;
    }

    const std::string wanted = to_lower(format_nvml_uuid(gpu.uuid));

    unsigned int count = 0;

    if (count_fn(&count) != NVML_SUCCESS) {
        return;
    }

    for (unsigned int i = 0; i < count; i++) {
        void* device = nullptr;

        char uuid[NVML_UUID_BUFFER_SIZE] = {};

        if (handle_fn(i, &device) != NVML_SUCCESS || uuid_fn(device, uuid, NVML_UUID_BUFFER_SIZE) != NVML_SUCCESS) {
            continue;
        }

        if (to_lower(uuid) == wanted) {
            m_nvml_device = device;
            return;
        }
    }
}

uint32_t read_hex_file(const std::filesystem::path& path)
{
    std::ifstream file(path);

    std::string text;
    file >> text;

    try {
        return static_cast<uint32_t>(std::stoul(text, nullptr, 16));
    }

    catch (...) {
        return 0;
    }
}

void init_sysfs(const gpu_identity& gpu)
{
    std::error_code error;

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator("/sys/class/drm", error)) {
        const std::string name = entry.path().filename().string();

        if (name.rfind("card", 0) != 0 || name.find('-') != std::string::npos) {
            continue;
        }

        const std::filesystem::path device = entry.path() / "device";

        if (read_hex_file(device / "vendor") != gpu.vendor_id || read_hex_file(device / "device") != gpu.device_id) {
            continue;
        }

        if (std::filesystem::exists(device / "gpu_busy_percent", error)) {
            m_sysfs_busy_path = (device / "gpu_busy_percent").string();
            return;
        }
    }
}

void init_gpu(const gpu_identity& gpu)
{
    if (gpu.vendor_id == NVIDIA_VENDOR_ID) {
        init_nvml(gpu);
    }

    if (gpu.vendor_id == AMD_VENDOR_ID) {
        init_sysfs(gpu);
    }
}

void cleanup_gpu()
{
    if (m_nvml_library != nullptr) {
        m_nvml_shutdown();

        dlclose(m_nvml_library);
    }

    m_nvml_library     = nullptr;
    m_nvml_device      = nullptr;
    m_nvml_shutdown    = nullptr;
    m_nvml_utilization = nullptr;

    m_sysfs_busy_path.clear();
}

void sample_gpu()
{
    if (m_nvml_device != nullptr) {
        nvml_utilization utilization = {};

        if (m_nvml_utilization(m_nvml_device, &utilization) == NVML_SUCCESS) {
            m_gpu_load = std::clamp(static_cast<float>(utilization.gpu) * PERCENT_TO_LOAD, 0.0f, 1.0f);
        }

        return;
    }

    if (!m_sysfs_busy_path.empty()) {
        std::ifstream file(m_sysfs_busy_path);

        int percent = 0;

        if (file >> percent) {
            m_gpu_load = std::clamp(static_cast<float>(percent) * PERCENT_TO_LOAD, 0.0f, 1.0f);
        }
    }
}

#endif

}

void init(const gpu_identity& gpu)
{
    if (m_is_initialized) {
        return;
    }

    m_is_initialized = true;

    sample_cpu();
    sample_memory();

    init_gpu(gpu);
}

void cleanup()
{
    if (!m_is_initialized) {
        return;
    }

    cleanup_gpu();

    m_cpu_load = UNAVAILABLE;
    m_gpu_load = UNAVAILABLE;

    m_ram  = {};
    m_swap = {};

    m_last_cpu_idle  = 0;
    m_last_cpu_total = 0;

    m_is_initialized = false;
}

void sample()
{
    if (!m_is_initialized) {
        return;
    }

    sample_cpu();
    sample_gpu();
    sample_memory();
}

float get_cpu_load() { return m_cpu_load; }
float get_gpu_load() { return m_gpu_load; }

const memory_usage& get_ram_usage()  { return m_ram; }
const memory_usage& get_swap_usage() { return m_swap; }

}
