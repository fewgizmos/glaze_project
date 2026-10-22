#ifndef NDEBUG

#include "glp/trainer/ui/file_field.h"

#include "glp/glaze/rendering/scene_render/resources/textures.h"

#include <ImGuiFileDialog.h>

#include <imgui.h>

#include <array>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace trainer::ui::file_field
{

namespace
{

namespace textures = glaze::rendering::scene_render::res::textures;

class file_dialog : public IGFD::FileDialog
{
public:
    void quit()
    {
        if (IsOpened()) {
            m_QuitFrame();
        }

        Close();
    }
};

struct image_preview
{
    std::string path;

    const glaze::rendering::scene_render::texture* target = nullptr;
};

constexpr const char* RESOURCES_ROOT = "source/glaze_project_resources";

constexpr const char* BROWSE_LABEL = "...";

constexpr const char* DIALOG_WINDOW_ID = "###file_field_dialog";

constexpr size_t MAX_PATH_LENGTH = 1024;

constexpr uint32_t PREVIEW_MIP_CHAIN = 0;

const ImVec2 DIALOG_SIZE     = ImVec2(820.0f, 520.0f);
const ImVec2 DIALOG_MIN_SIZE = ImVec2(480.0f, 300.0f);
const ImVec2 DIALOG_MAX_SIZE = ImVec2(FLT_MAX, FLT_MAX);

const ImVec2 PREVIEW_SIZE = ImVec2(96.0f, 96.0f);

std::unique_ptr<file_dialog> m_dialog;

bool m_dialog_drawn = false;

std::unordered_map<std::string, image_preview> m_previews;

void create_thumbnail(IGFD_Thumbnail_Info* info)
{
    if (info == nullptr || !info->isReadyToUpload || info->textureFileDatas == nullptr) {
        return;
    }

    const glaze::rendering::scene_render::texture* created = textures::build_raw(
        info->textureFileDatas,
        static_cast<uint32_t>(info->textureWidth),
        static_cast<uint32_t>(info->textureHeight),
        false,
        1
    );

    delete[] info->textureFileDatas;

    info->textureFileDatas = nullptr;
    info->textureID        = reinterpret_cast<void*>(static_cast<uintptr_t>(created->index));
    info->userDatas        = const_cast<glaze::rendering::scene_render::texture*>(created);
    info->isReadyToUpload  = false;
    info->isReadyToDisplay = true;
}

void destroy_thumbnail(IGFD_Thumbnail_Info* info)
{
    if (info == nullptr || info->userDatas == nullptr) {
        return;
    }

    textures::erase(static_cast<const glaze::rendering::scene_render::texture*>(info->userDatas));

    info->userDatas        = nullptr;
    info->textureID        = nullptr;
    info->isReadyToDisplay = false;
}

file_dialog& get_dialog()
{
    if (!m_dialog) {
        m_dialog = std::make_unique<file_dialog>();

        m_dialog->SetCreateThumbnailCallback(create_thumbnail);
        m_dialog->SetDestroyThumbnailCallback(destroy_thumbnail);
    }

    return *m_dialog;
}

std::string start_directory(const std::string& path)
{
    std::error_code error;

    const std::filesystem::path current = path;

    if (!path.empty() && std::filesystem::is_directory(current, error)) {
        return current.string();
    }

    if (!path.empty() && std::filesystem::is_directory(current.parent_path(), error)) {
        return current.parent_path().string();
    }

    return RESOURCES_ROOT;
}

std::string relative_to_working_directory(const std::string& path)
{
    std::error_code error;

    const std::filesystem::path relative = std::filesystem::relative(path, std::filesystem::current_path(error), error);

    return error || relative.empty() ? path : relative.generic_string();
}

void release_preview(image_preview& preview)
{
    if (preview.target != nullptr) {
        textures::erase(preview.target);
    }

    preview = {};
}

}

bool draw(const char* label, std::string& path, const char* filters, pick_mode mode)
{
    file_dialog& dialog = get_dialog();

    bool committed = false;

    ImGui::PushID(label);

    const float browse_width = ImGui::CalcTextSize(BROWSE_LABEL).x + ImGui::GetStyle().FramePadding.x * 2.0f;

    std::array<char, MAX_PATH_LENGTH> buffer = {};
    std::strncpy(buffer.data(), path.c_str(), buffer.size() - 1);

    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - browse_width - ImGui::GetStyle().ItemInnerSpacing.x);

    if (ImGui::InputText("##path", buffer.data(), buffer.size())) {
        path = buffer.data();
    }

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        committed = true;
    }

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

    const std::string key = std::string("file_field_") + label;

    if (ImGui::Button(BROWSE_LABEL)) {
        dialog.quit();

        IGFD::FileDialogConfig config;
        config.path  = start_directory(path);
        config.flags = ImGuiFileDialogFlags_NoDialog;

        dialog.OpenDialog(key, label, mode == pick_mode::folder ? nullptr : filters, config);
    }

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(label);

    if (dialog.IsOpened(key)) {
        m_dialog_drawn = true;

        bool is_open = true;

        ImGui::SetNextWindowSize(DIALOG_SIZE, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(DIALOG_MIN_SIZE, DIALOG_MAX_SIZE);

        const std::string title = std::string(label) + DIALOG_WINDOW_ID;

        if (ImGui::Begin(title.c_str(), &is_open, ImGuiWindowFlags_NoCollapse)) {
            if (dialog.Display(key, ImGuiWindowFlags_NoCollapse, ImGui::GetContentRegionAvail())) {
                if (dialog.IsOk()) {
                    const std::string chosen = mode == pick_mode::folder ? dialog.GetCurrentPath() : dialog.GetFilePathName();

                    path = relative_to_working_directory(chosen);

                    committed = true;
                }

                is_open = false;
            }
        }

        ImGui::End();

        if (!is_open) {
            dialog.quit();
        }
    }

    ImGui::PopID();

    return committed;
}

void draw_image_preview(const char* id, const std::string& image_path)
{
    image_preview& preview = m_previews[id];

    if (preview.path != image_path) {
        release_preview(preview);

        preview.path = image_path;

        std::error_code error;

        if (!image_path.empty() && std::filesystem::is_regular_file(image_path, error)) {
            try {
                preview.target = textures::build_from_file(image_path, false, PREVIEW_MIP_CHAIN);
            }

            catch (const std::exception&) {
                preview.target = nullptr;
            }
        }
    }

    if (preview.target == nullptr) {
        ImGui::Dummy(PREVIEW_SIZE);
        return;
    }

    ImGui::Image(static_cast<ImTextureID>(preview.target->index), PREVIEW_SIZE);
}

void update()
{
    file_dialog& dialog = get_dialog();

    if (!m_dialog_drawn && dialog.IsOpened()) {
        dialog.quit();
    }

    m_dialog_drawn = false;

    dialog.ManageGPUThumbnails();
}

void cleanup()
{
    for (auto& [id, preview] : m_previews) {
        release_preview(preview);
    }

    m_previews.clear();

    if (!m_dialog) {
        return;
    }

    m_dialog->quit();
    m_dialog->ManageGPUThumbnails();

    m_dialog.reset();
}

}

#endif
