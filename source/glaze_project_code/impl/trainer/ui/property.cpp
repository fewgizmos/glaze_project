#include "glp/trainer/ui/property.h"

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include <imgui.h>
#include <imgui_internal.h>

#include <cctype>
#include <cfloat>

namespace trainer::ui::property
{

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
    const std::string name = display_name(label);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name.c_str());

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && ImGui::GetItemRectSize().x > ImGui::GetColumnWidth()) {
        ImGui::SetTooltip("%s", name.c_str());
    }

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);

    return std::string("##") + label;
}

bool begin_category(const char* name)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    const ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_DefaultOpen
        | ImGuiTreeNodeFlags_NoTreePushOnOpen
        | ImGuiTreeNodeFlags_Framed
        | ImGuiTreeNodeFlags_SpanAllColumns;

    return ImGui::TreeNodeEx(name, flags, "%s", display_name(name).c_str());
}

}

#endif
