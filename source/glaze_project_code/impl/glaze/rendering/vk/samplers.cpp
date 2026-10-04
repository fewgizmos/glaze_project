#include "glp/glaze/rendering/vk/samplers.h"

#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/defines.h"

#include "glp/glaze/utilities/core/assert.h"

#include <algorithm>
#include <vector>

namespace glaze::rendering::vk::samplers
{

namespace
{

struct made_sampler
{
    sampler_create_info create_info;
    VkSampler handle = VK_NULL_HANDLE;
};

constexpr float MAX_ANISOTROPY = 16.0f;

std::vector<made_sampler> m_samplers;

bool is_same(const sampler_create_info& left, const sampler_create_info& right)
{
    return left.mag_filter == right.mag_filter
        && left.min_filter == right.min_filter
        && left.mip_mode   == right.mip_mode
        && left.wrap_u     == right.wrap_u
        && left.wrap_v     == right.wrap_v
        && left.anisotropy == right.anisotropy;
}

}

uint32_t make(const sampler_create_info& create_info)
{
    for (uint32_t i = 0; i < m_samplers.size(); i++) {
        if (is_same(m_samplers[i].create_info, create_info)) {
            return i;
        }
    }

    GLAZE_ASSERT(m_samplers.size() >= MAX_SAMPLERS, "samplers limit exceeded");

    VkSamplerCreateInfo vk_create_info = {};
    vk_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    vk_create_info.magFilter = create_info.mag_filter;
    vk_create_info.minFilter = create_info.min_filter;
    vk_create_info.mipmapMode = create_info.mip_mode;
    vk_create_info.addressModeU = create_info.wrap_u;
    vk_create_info.addressModeV = create_info.wrap_v;
    vk_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    vk_create_info.anisotropyEnable = create_info.anisotropy ? VK_TRUE : VK_FALSE;
    vk_create_info.maxAnisotropy = std::min(
        MAX_ANISOTROPY,
        device::get_physical().properties.limits.maxSamplerAnisotropy
    );
    vk_create_info.minLod = 0.0f;
    vk_create_info.maxLod = VK_LOD_CLAMP_NONE;

    made_sampler made;
    made.create_info = create_info;

    const VkResult result = device::get_disp().createSampler(&vk_create_info, nullptr, &made.handle);

    GLAZE_ASSERT(result != VK_SUCCESS, "failed to create sampler");

    m_samplers.push_back(made);

    return static_cast<uint32_t>(m_samplers.size() - 1);
}

VkSampler get(uint32_t index) { return m_samplers.at(index).handle; }

uint32_t get_count() { return static_cast<uint32_t>(m_samplers.size()); }

void destroy_all()
{
    for (const made_sampler& made : m_samplers) {
        device::get_disp().destroySampler(made.handle, nullptr);
    }

    m_samplers.clear();
}

}
