#include "glp/trainer/objects/field_editors.h"

#include "glp/glaze/runtime/config.h"

#ifdef DEV_BUILD

#include "glp/trainer/objects/object_types.h"
#include "glp/trainer/ui/file_field.h"

#include "glp/glaze/world/resources/textured_plane.h"

#include <imgui.h>

#include <cctype>
#include <cfloat>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>

namespace trainer::objects
{

namespace
{

constexpr const char* BASE_COLOR_SUFFIX = "_basecolor";

constexpr const char* PLANE_SERVICE_PREVIEW = "plane_service_preview";
constexpr const char* PLANE_DRAFT_PREVIEW   = "plane_draft_preview";

constexpr float SPEED_SMALL  = 0.01f;
constexpr float SPEED_MEDIUM = 0.1f;

glaze::world::res::textured_plane_create_info m_plane_draft;

uint64_t m_plane_edit_entity = 0;
glaze::world::res::textured_plane_create_info m_plane_edit;

std::string to_lower(std::string text)
{
    for (char& symbol : text) {
        symbol = static_cast<char>(std::tolower(static_cast<unsigned char>(symbol)));
    }

    return text;
}

bool is_pack_path(const std::string& path)
{
    std::error_code error;

    return path.empty() || std::filesystem::is_directory(path, error);
}

std::string find_base_color(const std::string& pack_path)
{
    std::error_code error;

    if (pack_path.empty() || !std::filesystem::is_directory(pack_path, error)) {
        return {};
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(pack_path, error)) {
        const std::string stem = to_lower(entry.path().stem().string());

        if (entry.is_regular_file(error) && stem.ends_with(BASE_COLOR_SUFFIX)) {
            return entry.path().string();
        }
    }

    return {};
}

bool edit_plane(glaze::world::res::textured_plane_create_info& info, const char* preview_id)
{
    bool committed = false;

    committed |= ui::file_field::draw("texture pack", info.pack_path, nullptr, ui::file_field::pick_mode::folder);

    ui::file_field::draw_image_preview(preview_id, find_base_color(info.pack_path));

    edit_float("size x", info.size_x, SPEED_MEDIUM, 0.0f, FLT_MAX);
    committed |= ImGui::IsItemDeactivatedAfterEdit();

    edit_float("size z", info.size_z, SPEED_MEDIUM, 0.0f, FLT_MAX);
    committed |= ImGui::IsItemDeactivatedAfterEdit();

    edit_vec2("tile size", info.tile_size, SPEED_SMALL);
    committed |= ImGui::IsItemDeactivatedAfterEdit();

    committed |= edit_bool("flip normal y", info.flip_normal_y);

    edit_float("transmission", info.transmission_factor, SPEED_SMALL, 0.0f, 1.0f);
    committed |= ImGui::IsItemDeactivatedAfterEdit();

    edit_float("parallax strength", info.parallax_strength, SPEED_SMALL, 0.0f, FLT_MAX);
    committed |= ImGui::IsItemDeactivatedAfterEdit();

    if (!is_pack_path(info.pack_path)) {
        ImGui::TextDisabled("texture pack folder not found");

        return false;
    }

    return committed;
}

void draw_plane_fields(glaze::scene::scene_entity* entity)
{
    glaze::world::res::cmp_textured_plane* target = find_component<glaze::world::res::cmp_textured_plane>(entity);

    if (target == nullptr) {
        return;
    }

    if (m_plane_edit_entity != entity->get_id()) {
        m_plane_edit_entity = entity->get_id();
        m_plane_edit        = target->get_create_info();
    }

    if (!edit_plane(m_plane_edit, PLANE_SERVICE_PREVIEW)) {
        return;
    }

    try {
        target->set_create_info(m_plane_edit);
    }

    catch (const std::exception& error) {
        report_error(error.what());
    }
}

glaze::scene::scene_entity* create_plane(glm::vec3 position)
{
    if (!is_pack_path(m_plane_draft.pack_path)) {
        report_error("texture pack folder not found: " + m_plane_draft.pack_path);
        return nullptr;
    }

    glaze::world::res::textured_plane_create_info info = m_plane_draft;
    info.position = position;

    try {
        return glaze::scene::it::create_entity<glaze::world::res::textured_plane>(info);
    }

    catch (const std::exception& error) {
        report_error(error.what());
        return nullptr;
    }
}

const bool registered = [] {
    object_type type;
    type.name         = "textured_plane";
    type.label        = "Textured Plane";
    type.is_creatable = true;
    type.matches      = is_type<glaze::world::res::textured_plane>;
    type.draw_fields  = draw_plane_fields;
    type.draw_draft   = [] { edit_plane(m_plane_draft, PLANE_DRAFT_PREVIEW); };
    type.create       = create_plane;

    register_type(type);

    return true;
}();

}

}

#endif
