#include "glp/trainer/ui/property.h"

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include <imgui.h>

#include <cctype>
#include <cfloat>

namespace trainer::ui::property
{

namespace
{

constexpr float LABEL_SHARE = 0.4f;

}

std::string display_name(const char* label)
{
    std::string name = label;

    bool is_word_start = true;

    for (char& symbol : name) {
        if (symbol == '_') {
            symbol = ' ';
        }

        if (is_word_start) {
            symbol = static_cast<char>(std::toupper(static_cast<unsigned char>(symbol)));
        }

        is_word_start = symbol == ' ';
    }

    return name;
}

std::string begin_row(const char* label)
{
    const float start_x = ImGui::GetCursorPosX();
    const float width   = ImGui::GetContentRegionAvail().x;

    const std::string name = display_name(label);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name.c_str());

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && ImGui::GetItemRectSize().x > width * LABEL_SHARE) {
        ImGui::SetTooltip("%s", name.c_str());
    }

    ImGui::SameLine(start_x + width * LABEL_SHARE);
    ImGui::SetNextItemWidth(-FLT_MIN);

    return std::string("##") + label;
}

bool begin_category(const char* name)
{
    const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth;

    return ImGui::TreeNodeEx(name, flags, "%s", display_name(name).c_str());
}

}

#endif
