#include "glp/glaze/rendering/imgui_render/imgui_render.h"

#include "glp/glaze/rendering/scene_render/scene_render.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"
#include "glp/glaze/rendering/scene_render/resources/textures.h"

#include "glp/glaze/rendering/vk/commands.h"
#include "glp/glaze/rendering/vk/device.h"
#include "glp/glaze/rendering/vk/frame.h"
#include "glp/glaze/rendering/vk/resources.h"
#include "glp/glaze/rendering/vk/samplers.h"
#include "glp/glaze/rendering/vk/shaders.h"

#include "glp/glaze/rendering/defines.h"

#include "glp/glaze/platform/app.h"

#include "glp/glaze/io/input.h"
#include "glp/glaze/io/viewport.h"

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace glaze::rendering::imgui_render
{

namespace
{

struct imgui_push
{
    glm::vec2 scale;
    glm::vec2 translate;
    uint32_t texture;
    uint32_t sampler;
};

constexpr VkDeviceSize INITIAL_VERTEX_COUNT = 16384;
constexpr VkDeviceSize INITIAL_INDEX_COUNT  = 32768;

constexpr VkIndexType INDEX_TYPE = sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

const scene_render::shader* m_shader = nullptr;

uint32_t m_sampler = 0;

buffer m_vertex_buffers[MAX_FRAMES_IN_FLIGHT];
buffer m_index_buffers[MAX_FRAMES_IN_FLIGHT];

std::vector<ImDrawVert> m_vertices;
std::vector<ImDrawIdx> m_indices;

void create_shader()
{
    graphics_pipeline_create_info info;
    info.color_formats        = { vk::frame::get_swapchain().image_format };
    info.cull_mode            = VK_CULL_MODE_NONE;
    info.depth_write          = false;
    info.alpha_blend          = true;
    info.set_layouts          = { scene_render::it::get_scene_set_layout() };
    info.push_constant_size   = sizeof(imgui_push);
    info.push_constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    info.vertex_bindings = { { 0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX } };

    info.vertex_attributes = {
        { 0, 0, VK_FORMAT_R32G32_SFLOAT,  static_cast<uint32_t>(offsetof(ImDrawVert, pos)) },
        { 1, 0, VK_FORMAT_R32G32_SFLOAT,  static_cast<uint32_t>(offsetof(ImDrawVert, uv)) },
        { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, static_cast<uint32_t>(offsetof(ImDrawVert, col)) }
    };

    m_shader = scene_render::res::shaders::build("imgui", "sys/ui/imgui.vert", "sys/ui/imgui.frag", "", info, {}, scene_render::render_queue::opaque);
}

void create_sampler()
{
    sampler_create_info sampler_info;
    sampler_info.mip_mode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.wrap_u     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_v     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.wrap_w     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropy = false;

    m_sampler = vk::samplers::make(sampler_info);
}

buffer create_geometry_buffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
    return vk::resources::create_buffer(size, usage, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
}

void ensure_capacity(buffer& target, VkDeviceSize size, VkBufferUsageFlags usage)
{
    if (target.size >= size) {
        return;
    }

    vk::resources::destroy_buffer(target);

    target = create_geometry_buffer(std::max(size, target.size * 2), usage);
}

void destroy_texture(ImTextureData* data)
{
    const scene_render::texture* target = static_cast<const scene_render::texture*>(data->BackendUserData);

    if (target != nullptr) {
        scene_render::res::textures::erase(target);
    }

    data->SetTexID(ImTextureID_Invalid);
    data->BackendUserData = nullptr;
}

void upload_texture(ImTextureData* data)
{
    destroy_texture(data);

    const scene_render::texture* target = scene_render::res::textures::build_raw(data->GetPixels(), data->Width, data->Height, false, 1);

    data->SetTexID(static_cast<ImTextureID>(target->index));
    data->BackendUserData = const_cast<scene_render::texture*>(target);
}

void update_textures()
{
    for (ImTextureData* data : ImGui::GetPlatformIO().Textures) {
        switch (data->Status) {
            case ImTextureStatus_WantCreate:
            case ImTextureStatus_WantUpdates:
                upload_texture(data);
                data->SetStatus(ImTextureStatus_OK);
                break;

            case ImTextureStatus_WantDestroy:
                destroy_texture(data);
                data->SetStatus(ImTextureStatus_Destroyed);
                break;

            default:
                break;
        }
    }
}

void upload_geometry(const ImDrawData* draw_data, uint32_t frame)
{
    m_vertices.clear();
    m_indices.clear();

    for (const ImDrawList* list : draw_data->CmdLists) {
        m_vertices.insert(m_vertices.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
        m_indices.insert(m_indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
    }

    const VkDeviceSize vertex_size = sizeof(ImDrawVert) * m_vertices.size();
    const VkDeviceSize index_size  = sizeof(ImDrawIdx) * m_indices.size();

    ensure_capacity(m_vertex_buffers[frame], vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    ensure_capacity(m_index_buffers[frame], index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    vk::resources::upload_buffer(m_vertex_buffers[frame], m_vertices.data(), vertex_size);
    vk::resources::upload_buffer(m_index_buffers[frame], m_indices.data(), index_size);
}

}

void init_all()
{
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename   = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForVulkan(io::viewport::get_native());

    create_sampler();
    create_shader();

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_vertex_buffers[i] = create_geometry_buffer(sizeof(ImDrawVert) * INITIAL_VERTEX_COUNT, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        m_index_buffers[i]  = create_geometry_buffer(sizeof(ImDrawIdx) * INITIAL_INDEX_COUNT, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }
}

void clean_all()
{
    for (ImTextureData* data : ImGui::GetPlatformIO().Textures) {
        if (data->RefCount == 1) {
            destroy_texture(data);
            data->SetStatus(ImTextureStatus_Destroyed);
        }
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::resources::destroy_buffer(m_vertex_buffers[i]);
        vk::resources::destroy_buffer(m_index_buffers[i]);
    }

    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    m_shader = nullptr;

    m_vertices.clear();
    m_indices.clear();
}

void begin_frame()
{
    for (const SDL_Event& event : platform::app::get_events()) {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }

    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    const ImGuiIO& io = ImGui::GetIO();

    io::input::set_capture(io.WantCaptureMouse, io.WantCaptureKeyboard);
}

void end_frame()
{
    ImGui::Render();

    update_textures();
}

void draw_all()
{
    const ImDrawData* draw_data = ImGui::GetDrawData();

    if (draw_data == nullptr || draw_data->TotalVtxCount == 0) {
        return;
    }

    const vkb::DispatchTable& disp = vk::device::get_disp();

    VkCommandBuffer cmd = vk::frame::get_current_frame().command_buffer;

    const uint32_t frame = vk::frame::get_current_frame_index();

    upload_geometry(draw_data, frame);

    const VkDescriptorSet scene_set = scene_render::it::get_scene_set();

    const VkDeviceSize vertex_offset = 0;

    disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shader->graphics.handle);
    disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shader->graphics.layout, 0, 1, &scene_set, 0, nullptr);
    disp.cmdBindVertexBuffers(cmd, 0, 1, &m_vertex_buffers[frame].handle, &vertex_offset);
    disp.cmdBindIndexBuffer(cmd, m_index_buffers[frame].handle, 0, INDEX_TYPE);

    imgui_push push;
    push.scale     = glm::vec2(2.0f / draw_data->DisplaySize.x, 2.0f / draw_data->DisplaySize.y);
    push.translate = glm::vec2(-1.0f) - glm::vec2(draw_data->DisplayPos.x, draw_data->DisplayPos.y) * push.scale;
    push.sampler   = m_sampler;

    const VkExtent2D extent = vk::frame::get_swapchain().extent;

    const ImVec2 clip_offset = draw_data->DisplayPos;
    const ImVec2 clip_scale  = draw_data->FramebufferScale;

    uint32_t global_vertex = 0;
    uint32_t global_index  = 0;

    for (const ImDrawList* list : draw_data->CmdLists) {
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.UserCallback != nullptr) {
                command.UserCallback(list, &command);
                continue;
            }

            const float min_x = std::max((command.ClipRect.x - clip_offset.x) * clip_scale.x, 0.0f);
            const float min_y = std::max((command.ClipRect.y - clip_offset.y) * clip_scale.y, 0.0f);
            const float max_x = std::min((command.ClipRect.z - clip_offset.x) * clip_scale.x, static_cast<float>(extent.width));
            const float max_y = std::min((command.ClipRect.w - clip_offset.y) * clip_scale.y, static_cast<float>(extent.height));

            if (max_x <= min_x || max_y <= min_y) {
                continue;
            }

            VkRect2D scissor = {};
            scissor.offset = { static_cast<int32_t>(min_x), static_cast<int32_t>(min_y) };
            scissor.extent = { static_cast<uint32_t>(max_x - min_x), static_cast<uint32_t>(max_y - min_y) };

            disp.cmdSetScissor(cmd, 0, 1, &scissor);

            push.texture = static_cast<uint32_t>(command.GetTexID());

            vk::commands::push_constants(cmd, m_shader->graphics.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(push), &push);

            disp.cmdDrawIndexed(cmd, command.ElemCount, 1, command.IdxOffset + global_index, static_cast<int32_t>(command.VtxOffset + global_vertex), 0);
        }

        global_vertex += static_cast<uint32_t>(list->VtxBuffer.Size);
        global_index  += static_cast<uint32_t>(list->IdxBuffer.Size);
    }

    VkRect2D full = {};
    full.extent = extent;

    disp.cmdSetScissor(cmd, 0, 1, &full);
}

}
