#ifndef NDEBUG

#include "glp/trainer/ui/ui.h"

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

void draw_time_of_day()
{
    ImGui::SetNextWindowSize(TIME_WINDOW_SIZE, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(1.0f);

    if (!ImGui::Begin("Time of day", &m_time_open)) {
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

    if (ImGui::SliderFloat("##time_of_day", &time_of_day, MIN_TIME_OF_DAY, MAX_TIME_OF_DAY, clock)) {
        sky->set_time_of_day(time_of_day);
    }

    ImGui::SameLine();
    ImGui::Text("%.3f", time_of_day);

    ImGui::End();
}

}

void draw()
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            if (ImGui::BeginMenu("Trainer")) {
                ImGui::MenuItem("Time of day", nullptr, &m_time_open);
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    if (m_time_open) {
        draw_time_of_day();
    }
}

}

#endif
