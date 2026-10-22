#pragma once

#include <cstdint>

namespace glaze::utilities::system_load
{

struct gpu_identity
{
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;

    uint8_t uuid[16] = {};

    uint8_t luid[8] = {};
    bool luid_valid = false;
};

struct memory_usage
{
    uint64_t used  = 0;
    uint64_t total = 0;

    bool available = false;
};

void init(const gpu_identity& gpu);
void cleanup();

void sample();

float get_cpu_load();
float get_gpu_load();

const memory_usage& get_ram_usage();
const memory_usage& get_swap_usage();

}
