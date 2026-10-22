#ifndef NDEBUG

#include "glp/trainer/objects/selection.h"

#include "glp/trainer/objects/object_types.h"
#include "glp/trainer/ui/metrics.h"

#include "glp/glaze/io/input.h"

#include "glp/glaze/rendering/scene_render/frame_pipeline.h"
#include "glp/glaze/rendering/scene_render/picking.h"
#include "glp/glaze/rendering/scene_render/post_process.h"

#include "glp/glaze/scene/volume/volume.h"

#include <ImGuizmo.h>

#include <imgui.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace trainer::objects::selection
{

namespace
{

namespace picking = glaze::rendering::scene_render::picking;

enum class gizmo_mode
{
    move,
    rotate,
    scale
};

constexpr uint64_t NO_ENTITY = 0;

constexpr float SPAWN_DISTANCE = 5.0f;

constexpr float GIZMO_FAR_PLANE = 100000.0f;

constexpr float MIN_SCALE = 0.0001f;

constexpr float MIN_PANEL_MARGIN_Y = 0.02f;
constexpr float MIN_PANEL_MARGIN_X = 0.012f;

const glm::vec2 SCREEN_CENTER = glm::vec2(0.5f);

uint64_t m_selected = NO_ENTITY;

gizmo_mode m_mode = gizmo_mode::move;

uint64_t m_click_request = 0;
uint64_t m_spawn_request = 0;

std::function<glaze::scene::scene_entity*(glm::vec3)> m_spawn_create;

glaze::scene::scene_entity* find_entity(uint64_t entity_id)
{
    for (const std::unique_ptr<glaze::scene::scene_entity>& entity : glaze::scene::it::get_all_entities()) {
        if (entity->get_id() == entity_id && !glaze::scene::it::util::is_entity_pending_erase(entity.get())) {
            return entity.get();
        }
    }

    return nullptr;
}

glaze::scene::scene_entity* find_picked(uint32_t picked_id)
{
    if (picked_id == 0) {
        return nullptr;
    }

    for (const std::unique_ptr<glaze::scene::scene_entity>& entity : glaze::scene::it::get_all_entities()) {
        if ((entity->get_id() & picking::ENTITY_ID_MASK) == picked_id && !glaze::scene::it::util::is_entity_pending_erase(entity.get())) {
            return entity.get();
        }
    }

    return nullptr;
}

glm::vec3 world_from_pick(const picking::pick_result& result)
{
    const glaze::rendering::scene_render::gpu_camera& camera = glaze::rendering::scene_render::frame_pipe::get_camera();

    if (result.depth <= 0.0f) {
        const glm::vec3 forward = -glm::vec3(glm::inverse(camera.view)[2]);

        return camera.position + glm::normalize(forward) * SPAWN_DISTANCE;
    }

    const glm::vec4 world = camera.inv_view_proj * glm::vec4(result.uv * 2.0f - 1.0f, result.depth, 1.0f);

    return glm::vec3(world) / world.w;
}

void handle_results()
{
    picking::pick_result result;

    while (picking::take_result(result)) {
        if (result.request == m_click_request) {
            if (glaze::scene::scene_entity* picked = find_picked(result.entity_id)) {
                m_selected = picked->get_id();
            }
        }

        if (result.request == m_spawn_request && m_spawn_create) {
            if (glaze::scene::scene_entity* created = m_spawn_create(world_from_pick(result))) {
                m_selected = created->get_id();
            }

            m_spawn_create = nullptr;
        }
    }
}

void handle_input()
{
    const ImGuiIO& io = ImGui::GetIO();

    if (!io.WantCaptureKeyboard && glaze::io::input::key_is_pressed(glaze::io::input::keyboard_input_codes::esc)) {
        m_selected = NO_ENTITY;
    }

    const glaze::io::input::mouse& mouse = glaze::io::input::get_mouse();

    if (!mouse.button_is_pressed(glaze::io::input::mouse_input_codes::left)) {
        return;
    }

    if (io.WantCaptureMouse || ImGuizmo::IsOver() || ImGuizmo::IsUsing() || io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
        return;
    }

    m_click_request = picking::request(glm::vec2(io.MousePos.x / io.DisplaySize.x, io.MousePos.y / io.DisplaySize.y));
}

glm::mat4 gizmo_projection(const glm::mat4& proj)
{
    const float cot_half_fov = std::abs(proj[1][1]);
    const float aspect       = cot_half_fov / proj[0][0];
    const float near_plane   = proj[3][2];

    return glm::perspectiveRH_NO(2.0f * std::atan(1.0f / cot_half_fov), aspect, near_plane, GIZMO_FAR_PLANE);
}

glm::vec3 euler_from_rotation(const glm::mat3& rotation)
{
    const float x = std::asin(std::clamp(-rotation[2][1], -1.0f, 1.0f));
    const float y = std::atan2(rotation[2][0], rotation[2][2]);
    const float z = std::atan2(rotation[0][1], rotation[1][1]);

    return glm::degrees(glm::vec3(x, y, z));
}

void apply_matrix(glaze::scene::scene_entity* entity, const glm::mat4& matrix)
{
    const glm::vec3 position = glm::vec3(matrix[3]);

    glm::vec3 scale(glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])), glm::length(glm::vec3(matrix[2])));

    scale = glm::max(scale, glm::vec3(MIN_SCALE));

    const glm::mat3 rotation(glm::vec3(matrix[0]) / scale.x, glm::vec3(matrix[1]) / scale.y, glm::vec3(matrix[2]) / scale.z);

    entity->set_transform(position, euler_from_rotation(rotation), scale);
}

ImGuizmo::OPERATION gizmo_operation()
{
    switch (m_mode) {
        case gizmo_mode::move:   return ImGuizmo::TRANSLATE;
        case gizmo_mode::rotate: return ImGuizmo::ROTATE;
        case gizmo_mode::scale:  return ImGuizmo::SCALE;
    }

    return ImGuizmo::TRANSLATE;
}

void draw_gizmo(glaze::scene::scene_entity* entity)
{
    if (!glaze::rendering::scene_render::frame_pipe::has_camera()) {
        return;
    }

    const glaze::rendering::scene_render::gpu_camera& camera = glaze::rendering::scene_render::frame_pipe::get_camera();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y);

    const glm::mat4 projection = gizmo_projection(camera.proj);

    glm::mat4 matrix = glaze::scene::vol::compose_transform(entity->get_position(), entity->get_rotation(), entity->get_scale());

    const ImGuizmo::MODE space = m_mode == gizmo_mode::scale ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    if (ImGuizmo::Manipulate(glm::value_ptr(camera.view), glm::value_ptr(projection), gizmo_operation(), space, glm::value_ptr(matrix))) {
        apply_matrix(entity, matrix);
    }
}

void draw_mode_panel()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImVec2 metrics_position;
    ImVec2 metrics_size;

    const float gap = ImGui::GetStyle().ItemSpacing.x;

    ImVec2 position(
        viewport->WorkPos.x + viewport->WorkSize.x * (1.0f - MIN_PANEL_MARGIN_X),
        viewport->WorkPos.y + viewport->WorkSize.y * MIN_PANEL_MARGIN_Y
    );

    if (ui::metrics::get_minimized_bounds(metrics_position, metrics_size)) {
        position = ImVec2(metrics_position.x - gap, metrics_position.y);
    }

    ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2(1.0f, 0.0f));

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoMove;

    if (!ImGui::Begin("##gizmo_mode", nullptr, flags)) {
        ImGui::End();
        return;
    }

    int mode = static_cast<int>(m_mode);

    ImGui::RadioButton("Move", &mode, static_cast<int>(gizmo_mode::move));
    ImGui::SameLine();
    ImGui::RadioButton("Rotate", &mode, static_cast<int>(gizmo_mode::rotate));
    ImGui::SameLine();
    ImGui::RadioButton("Scale", &mode, static_cast<int>(gizmo_mode::scale));

    m_mode = static_cast<gizmo_mode>(mode);

    ImGui::End();
}

}

void update()
{
    ImGuizmo::BeginFrame();

    handle_results();
    handle_input();

    if (m_selected != NO_ENTITY && find_entity(m_selected) == nullptr) {
        m_selected = NO_ENTITY;
    }

    glaze::rendering::scene_render::post_process::set_selected_entity(m_selected);
}

void draw()
{
    glaze::scene::scene_entity* selected = get_selected();

    if (selected == nullptr) {
        return;
    }

    const object_type* type = find_type(selected);

    if (type == nullptr || !type->has_transform) {
        return;
    }

    draw_mode_panel();
    draw_gizmo(selected);
}

void select(uint64_t entity_id)
{
    m_selected = entity_id;
}

void clear()
{
    m_selected = NO_ENTITY;
}

glaze::scene::scene_entity* get_selected()
{
    return m_selected == NO_ENTITY ? nullptr : find_entity(m_selected);
}

void request_spawn(const std::function<glaze::scene::scene_entity*(glm::vec3)>& create)
{
    m_spawn_create  = create;
    m_spawn_request = picking::request(SCREEN_CENTER);
}

}

#endif
