#ifndef NDEBUG

#include "glp/trainer/ui/ui.h"
#include "glp/trainer/ui/file_field.h"
#include "glp/trainer/ui/metrics.h"
#include "glp/trainer/ui/scene_objects.h"

#include "glp/trainer/objects/selection.h"

#include "glp/glaze/rendering/scene_render/resources/shaders.h"

#include "glp/glaze/scene/scene.h"

#include "glp/glaze/world/resources/sky_atmosphere.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace trainer::ui::it
{

namespace
{

constexpr float MIN_TIME_OF_DAY = 0.0f;
constexpr float MAX_TIME_OF_DAY = 24.0f;

constexpr float MINUTES_PER_HOUR = 60.0f;

const ImVec2 TIME_WINDOW_SIZE = ImVec2(360.0f, 90.0f);

bool m_time_open = false;

void print_camera()
{
    glaze::scene::scene_entity* player = glaze::scene::it::get_player_entity();

    if (player == nullptr) {
        std::printf("camera: no player entity\n");
        return;
    }

    const glm::vec3 pos = player->scene_volume().w_pos;
    const glm::vec3 rot = player->scene_volume().w_rot;

    std::printf("camera pos: glm::vec3(%.3ff, %.3ff, %.3ff)\n", pos.x, pos.y, pos.z);
    std::printf("camera rot: glm::vec3(%.3ff, %.3ff, %.3ff)\n", rot.x, rot.y, rot.z);
    std::fflush(stdout);
}

void draw_time_of_day()
{
    ImGui::SetNextWindowSize(TIME_WINDOW_SIZE, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(1.0f);

    if (!ImGui::Begin("World Settings", &m_time_open)) {
        ImGui::End();
        return;
    }

    const std::vector<glaze::world::res::sky_atmosphere*> skies = glaze::scene::it::get_all_entities_typed<glaze::world::res::sky_atmosphere>();

    if (skies.empty()) {
        ImGui::TextUnformatted("no sky_atmosphere in scene");
        ImGui::End();
        return;
    }

    glaze::world::res::sky_atmosphere* sky = skies.front();

    float time_of_day = sky->get_time_of_day();

    int hours   = static_cast<int>(time_of_day);
    int minutes = static_cast<int>(std::lround((time_of_day - static_cast<float>(hours)) * MINUTES_PER_HOUR));

    if (minutes == static_cast<int>(MINUTES_PER_HOUR)) {
        hours++;
        minutes = 0;
    }

    char clock[16];
    std::snprintf(clock, sizeof(clock), "%d:%02d", hours, minutes);

    ImGui::BulletText("Time:");

    if (ImGui::SliderFloat("##time_of_day", &time_of_day, MIN_TIME_OF_DAY, MAX_TIME_OF_DAY, clock)) {
        sky->set_time_of_day(time_of_day);
    }

    ImGui::SameLine();
    ImGui::Text("%.3f", time_of_day);

    ImGui::End();
}

}

void cleanup()
{
    file_field::cleanup();
    metrics::cleanup();
}

void draw()
{
    metrics::update();

    objects::selection::update();

    file_field::update();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            metrics::draw_menu();
            scene_objects::draw_menu();
            ImGui::MenuItem("World Settings", nullptr, &m_time_open);

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Debug")) {
            if (ImGui::MenuItem("Reload Shaders")) {
                glaze::rendering::scene_render::res::shaders::reload_all();
            }

            if (ImGui::MenuItem("Print Camera Pos/Rot")) {
                print_camera();
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    metrics::draw();

    scene_objects::draw();

    objects::selection::draw();

    if (m_time_open) {
        draw_time_of_day();
    }
}

}

#endif
