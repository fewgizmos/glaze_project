#include "glp/glaze/rendering/scene_render/picking.h"

#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/resources.h"

#include "glp/glaze/rendering/defines.h"

#include <glm/common.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <deque>

namespace glaze::rendering::scene_render::picking
{

namespace
{

struct readback_data
{
    uint16_t normal[4];
    float depth;
    uint32_t padding0;
};

struct readback_slot
{
    buffer target;

    bool is_pending = false;

    uint64_t request = 0;

    glm::vec2 uv = glm::vec2(0.0f);
};

constexpr uint32_t NORMAL_TEXEL_SIZE = sizeof(uint16_t) * 4;

constexpr uint32_t ENTITY_ID_CHANNEL_BITS = 11;

readback_slot m_slots[MAX_FRAMES_IN_FLIGHT];

bool m_has_request = false;

glm::vec2 m_request_uv = glm::vec2(0.0f);

uint64_t m_next_request = 0;
uint64_t m_request_id   = 0;

std::deque<pick_result> m_results;

void read_slot(readback_slot& slot)
{
    if (!slot.is_pending) {
        return;
    }

    readback_data data = {};

    vk::resources::download_buffer(slot.target, &data, sizeof(data));

    const uint32_t low  = static_cast<uint32_t>(glm::unpackHalf1x16(data.normal[2]) + 0.5f);
    const uint32_t high = static_cast<uint32_t>(glm::unpackHalf1x16(data.normal[3]) + 0.5f);

    pick_result result;

    result.request   = slot.request;
    result.entity_id = low | (high << ENTITY_ID_CHANNEL_BITS);
    result.depth     = data.depth;
    result.uv        = slot.uv;

    m_results.push_back(result);

    slot.is_pending = false;
}

void copy_texel(VkCommandBuffer cmd, const image& source, VkImageAspectFlags aspect, VkImageLayout layout, VkPipelineStageFlags2 stage, VkAccessFlags2 access, VkOffset3D texel, const buffer& target, VkDeviceSize offset)
{
    vk::commands::transition_image(
        cmd,
        source.handle,
        layout,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        stage,
        access,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        aspect
    );

    VkBufferImageCopy region = {};
    region.bufferOffset = offset;
    region.imageSubresource.aspectMask = aspect;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = texel;
    region.imageExtent = { 1, 1, 1 };

    vk::device::get_disp().cmdCopyImageToBuffer(cmd, source.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target.handle, 1, &region);

    vk::commands::transition_image(
        cmd,
        source.handle,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        layout,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        0,
        stage,
        access,
        aspect
    );
}

}

void init_all()
{
    for (readback_slot& slot : m_slots) {
        slot.target = vk::resources::create_buffer(
            sizeof(readback_data),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
        );
    }
}

void clean_all()
{
    for (readback_slot& slot : m_slots) {
        vk::resources::destroy_buffer(slot.target);

        slot.is_pending = false;
    }

    m_results.clear();

    m_has_request = false;
}

uint64_t request(glm::vec2 uv)
{
    m_has_request = true;
    m_request_uv  = glm::clamp(uv, glm::vec2(0.0f), glm::vec2(1.0f));
    m_request_id  = ++m_next_request;

    return m_request_id;
}

void dispatch()
{
    readback_slot& slot = m_slots[vk::frame::get_current_frame_index()];

    read_slot(slot);

    if (!m_has_request) {
        return;
    }

    m_has_request = false;

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const image& normal = it::get_normal_image();
    const image& depth  = it::get_depth_image();

    const VkOffset3D texel = {
        std::min(static_cast<int32_t>(m_request_uv.x * static_cast<float>(normal.extent.width)), static_cast<int32_t>(normal.extent.width) - 1),
        std::min(static_cast<int32_t>(m_request_uv.y * static_cast<float>(normal.extent.height)), static_cast<int32_t>(normal.extent.height) - 1),
        0
    };

    copy_texel(cmd, normal, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, texel, slot.target, 0);
    copy_texel(cmd, depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, texel, slot.target, NORMAL_TEXEL_SIZE);

    slot.is_pending = true;
    slot.request    = m_request_id;
    slot.uv         = m_request_uv;
}

bool take_result(pick_result& out)
{
    if (m_results.empty()) {
        return false;
    }

    out = m_results.front();

    m_results.pop_front();

    return true;
}

}
