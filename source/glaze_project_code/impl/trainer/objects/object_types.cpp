#ifndef NDEBUG

#include "glp/trainer/objects/object_types.h"

#include "glp/trainer/scene/debug_camera.h"
#include "glp/trainer/scene/debug_grid.h"
#include "glp/trainer/ui/file_field.h"
#include "glp/trainer/ui/property.h"

#include "glp/glaze/scene/objects/light_object.h"
#include "glp/glaze/scene/objects/post_object.h"
#include "glp/glaze/scene/objects/static_camera_object.h"
#include "glp/glaze/scene/objects/static_object.h"

#include "glp/glaze/scene/volume/cmp_light.h"
#include "glp/glaze/scene/volume/cmp_model.h"
#include "glp/glaze/scene/volume/cmp_post.h"

#include "glp/glaze/world/resources/sky_atmosphere.h"
#include "glp/glaze/world/resources/skybox.h"
#include "glp/glaze/world/resources/textured_plane.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace trainer::objects
{

namespace
{

namespace component = glaze::scene::vol::component;

using glaze::scene::scene_entity;

constexpr const char* UNKNOWN_TYPE_NAME = "scene_entity";

constexpr const char* MODEL_FILTERS = "Models{.gltf,.glb}";

constexpr const char* BASE_COLOR_SUFFIX = "_basecolor";

constexpr const char* PLANE_SERVICE_PREVIEW = "plane_service_preview";
constexpr const char* PLANE_DRAFT_PREVIEW   = "plane_draft_preview";

constexpr float SPEED_FINE   = 0.001f;
constexpr float SPEED_SMALL  = 0.01f;
constexpr float SPEED_MEDIUM = 0.1f;
constexpr float SPEED_LARGE  = 1.0f;
constexpr float SPEED_HUGE   = 100.0f;

constexpr float MAX_ANGLE     = 360.0f;
constexpr float MAX_HOURS     = 24.0f;
constexpr float MAX_ELEVATION = 90.0f;

constexpr uint32_t MAX_GTAO_DIRECTIONS = 16;
constexpr uint32_t MAX_GTAO_STEPS      = 32;

const std::array<const char*, 3> LIGHT_TYPES = { "directional", "point", "ambient" };
const std::array<const char*, 3> TONEMAPPERS = { "pbr neutral", "aces", "agx" };
const std::array<const char*, 5> UPSCALE_MODES = { "native aa", "quality", "balanced", "performance", "ultra performance" };

std::string m_last_error;

std::string m_model_draft;

component::light_create_info m_light_draft;

glaze::world::res::textured_plane_create_info m_plane_draft;
glaze::world::res::atmosphere_create_info m_atmosphere_draft;
component::post_create_info m_post_draft;

uint64_t m_plane_edit_entity = 0;
glaze::world::res::textured_plane_create_info m_plane_edit;

template<typename component_type>
component_type* find_component(scene_entity* entity)
{
    for (const glaze::scene::vol::scene_volume* volume : { &entity->scene_volume(), &entity->scene_gizmo_volume() }) {
        for (const std::unique_ptr<glaze::scene::vol::volume_component>& target : volume->components) {
            if (component_type* found = dynamic_cast<component_type*>(target.get())) {
                return found;
            }
        }
    }

    return nullptr;
}

template<typename entity_type>
bool is_type(scene_entity* entity)
{
    return dynamic_cast<entity_type*>(entity) != nullptr;
}

std::string to_lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char symbol) { return static_cast<char>(std::tolower(symbol)); });

    return text;
}

bool is_model_path(const std::string& path)
{
    std::error_code error;

    const std::string extension = to_lower(std::filesystem::path(path).extension().string());

    return std::filesystem::is_regular_file(path, error) && (extension == ".gltf" || extension == ".glb");
}

bool is_pack_path(const std::string& path)
{
    std::error_code error;

    return path.empty() || std::filesystem::is_directory(path, error);
}

void report(const std::string& error)
{
    m_last_error = error;
}

bool edit_float(const char* label, float& value, float speed, float min = 0.0f, float max = 0.0f)
{
    return ImGui::DragFloat(ui::property::begin_row(label).c_str(), &value, speed, min, max);
}

bool edit_vec2(const char* label, glm::vec2& value, float speed)
{
    return ImGui::DragFloat2(ui::property::begin_row(label).c_str(), &value.x, speed);
}

bool edit_vec3(const char* label, glm::vec3& value, float speed)
{
    return ImGui::DragFloat3(ui::property::begin_row(label).c_str(), &value.x, speed, 0.0f, 0.0f, "%.6f");
}

bool edit_color(const char* label, glm::vec3& value)
{
    return ImGui::ColorEdit3(ui::property::begin_row(label).c_str(), &value.x, ImGuiColorEditFlags_Float);
}

bool edit_uint(const char* label, uint32_t& value, uint32_t min, uint32_t max)
{
    return ImGui::DragScalar(ui::property::begin_row(label).c_str(), ImGuiDataType_U32, &value, 1.0f, &min, &max);
}

bool edit_bool(const char* label, bool& value)
{
    return ImGui::Checkbox(ui::property::begin_row(label).c_str(), &value);
}

template<typename enum_type, size_t count>
bool edit_enum(const char* label, enum_type& value, const std::array<const char*, count>& names)
{
    int index = static_cast<int>(value);

    if (!ImGui::Combo(ui::property::begin_row(label).c_str(), &index, names.data(), static_cast<int>(count))) {
        return false;
    }

    value = static_cast<enum_type>(index);

    return true;
}

template<typename info_type>
struct info_field
{
    const char* name;
    std::function<bool(const char*, info_type&)> edit;
};

#define INFO_FIELD(member, editor) { #member, [edit = editor](const char* label, auto& info) { return edit(label, info.member); } }

auto float_editor(float speed, float min = 0.0f, float max = 0.0f)
{
    return [=](const char* label, float& value) { return edit_float(label, value, speed, min, max); };
}

auto vec3_editor(float speed)
{
    return [=](const char* label, glm::vec3& value) { return edit_vec3(label, value, speed); };
}

auto color_editor()
{
    return [](const char* label, glm::vec3& value) { return edit_color(label, value); };
}

auto uint_editor(uint32_t min, uint32_t max)
{
    return [=](const char* label, uint32_t& value) { return edit_uint(label, value, min, max); };
}

auto bool_editor()
{
    return [](const char* label, bool& value) { return edit_bool(label, value); };
}

template<size_t count>
auto enum_editor(const std::array<const char*, count>& names)
{
    return [&names](const char* label, auto& value) { return edit_enum(label, value, names); };
}

std::string first_word(const char* name)
{
    const char* end = std::strchr(name, '_');

    return end != nullptr ? std::string(name, end) : std::string(name);
}

template<typename info_type>
bool edit_fields(const std::vector<info_field<info_type>>& fields, info_type& info)
{
    std::vector<std::string> groups;

    for (const info_field<info_type>& field : fields) {
        groups.push_back(first_word(field.name));
    }

    for (std::string& group : groups) {
        for (const info_field<info_type>& field : fields) {
            const std::string word = first_word(field.name);

            if (word.size() < group.size() && group.starts_with(word)) {
                group = word;
            }
        }
    }

    std::vector<std::string> categories;

    for (const std::string& group : groups) {
        const bool is_shared = std::count(groups.begin(), groups.end(), group) > 1;

        if (is_shared && std::find(categories.begin(), categories.end(), group) == categories.end()) {
            categories.push_back(group);
        }
    }

    bool changed = false;

    for (size_t i = 0; i < fields.size(); i++) {
        if (std::find(categories.begin(), categories.end(), groups[i]) == categories.end()) {
            changed |= fields[i].edit(fields[i].name, info);
        }
    }

    for (const std::string& category : categories) {
        if (!ui::property::begin_category(category.c_str())) {
            continue;
        }

        for (size_t i = 0; i < fields.size(); i++) {
            if (groups[i] == category) {
                changed |= fields[i].edit(fields[i].name, info);
            }
        }
    }

    return changed;
}

bool edit_light(component::light_create_info& info)
{
    bool changed = false;

    changed |= edit_enum("type", info.type, LIGHT_TYPES);
    changed |= edit_color("color", info.color);
    changed |= edit_color("ground color", info.ground_color);
    changed |= edit_float("intensity", info.intensity, SPEED_MEDIUM, 0.0f, FLT_MAX);
    changed |= edit_float("range", info.range, SPEED_MEDIUM, 0.0f, FLT_MAX);
    changed |= edit_float("cone angle", info.cone_angle, SPEED_LARGE, 0.0f, MAX_ANGLE);
    changed |= edit_float("cone softness", info.cone_softness, SPEED_SMALL, 0.0f, 1.0f);
    changed |= edit_float("angular radius", info.angular_radius, SPEED_SMALL, 0.0f, MAX_ELEVATION);

    return changed;
}

const std::vector<info_field<component::post_create_info>>& post_fields()
{
    static const std::vector<info_field<component::post_create_info>> fields = {
        INFO_FIELD(ev100, float_editor(SPEED_SMALL)),
        INFO_FIELD(auto_exposure, bool_editor()),
        INFO_FIELD(exposure_compensation, float_editor(SPEED_SMALL)),
        INFO_FIELD(min_ev100, float_editor(SPEED_SMALL)),
        INFO_FIELD(max_ev100, float_editor(SPEED_SMALL)),
        INFO_FIELD(exposure_speed_up, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(exposure_speed_down, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(exposure_night_darkening, float_editor(SPEED_SMALL, 0.0f, 1.0f)),
        INFO_FIELD(exposure_center_weight, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(exposure_incident_metering, bool_editor()),
        INFO_FIELD(tonemapper, enum_editor(TONEMAPPERS)),
        INFO_FIELD(tonemap_contrast, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(tonemap_saturation, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(sharpen, float_editor(SPEED_SMALL, 0.0f, 1.0f)),
        INFO_FIELD(bloom_intensity, float_editor(SPEED_FINE, 0.0f, FLT_MAX)),
        INFO_FIELD(light_shafts_intensity, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(sun_glare_intensity, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(night_vision_intensity, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(gtao_radius, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(gtao_intensity, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(gtao_directions, uint_editor(1, MAX_GTAO_DIRECTIONS)),
        INFO_FIELD(gtao_steps, uint_editor(1, MAX_GTAO_STEPS)),
        INFO_FIELD(upscale_mode, enum_editor(UPSCALE_MODES)),
        INFO_FIELD(gpu_budget_fraction, float_editor(SPEED_SMALL, 0.0f, 1.0f)),
        INFO_FIELD(shadows_enabled, bool_editor()),
        INFO_FIELD(shadow_lod_bias, float_editor(SPEED_SMALL)),
        INFO_FIELD(shadow_softness, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(shadow_normal_bias, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(contact_shadow_length, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(volumetric_fog_enabled, bool_editor()),
        INFO_FIELD(volumetric_fog_distance, float_editor(SPEED_LARGE, 0.0f, FLT_MAX))
    };

    return fields;
}

bool edit_post(component::post_create_info& info)
{
    return edit_fields(post_fields(), info);
}

const std::vector<info_field<glaze::world::res::atmosphere_create_info>>& atmosphere_fields()
{
    static const std::vector<info_field<glaze::world::res::atmosphere_create_info>> fields = {
        INFO_FIELD(time_of_day, float_editor(SPEED_SMALL, 0.0f, MAX_HOURS)),
        INFO_FIELD(sun_max_elevation, float_editor(SPEED_MEDIUM, 0.0f, MAX_ELEVATION)),
        INFO_FIELD(sun_illuminance, float_editor(SPEED_HUGE, 0.0f, FLT_MAX)),
        INFO_FIELD(sun_angular_radius, float_editor(SPEED_FINE, 0.0f, MAX_ELEVATION)),
        INFO_FIELD(sun_light_scale, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(sun_temperature, float_editor(SPEED_HUGE, 0.0f, FLT_MAX)),
        INFO_FIELD(horizon_light_fade, float_editor(SPEED_MEDIUM, 0.0f, FLT_MAX)),
        INFO_FIELD(horizon_light_twilight, float_editor(SPEED_MEDIUM, 0.0f, FLT_MAX)),
        INFO_FIELD(horizon_light_desaturation, float_editor(SPEED_SMALL, 0.0f, 1.0f)),
        INFO_FIELD(evening_sun_pink, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(evening_halo_pink, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(moon_illuminance, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(moon_angular_radius, float_editor(SPEED_FINE, 0.0f, MAX_ELEVATION)),
        INFO_FIELD(moon_light_scale, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(moon_overlap_hours, float_editor(SPEED_SMALL, 0.0f, MAX_HOURS)),
        INFO_FIELD(moon_color, color_editor()),
        INFO_FIELD(moon_flare_size, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(moon_flare_falloff, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(moon_flare_multiplier, float_editor(SPEED_FINE, 0.0f, FLT_MAX)),
        INFO_FIELD(moon_flare_tint, color_editor()),
        INFO_FIELD(night_ambient_scale, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(day_sky_scale, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(twilight_desaturation, float_editor(SPEED_SMALL, 0.0f, 1.0f)),
        INFO_FIELD(night_sky_luminance, float_editor(SPEED_FINE, 0.0f, FLT_MAX)),
        INFO_FIELD(star_intensity, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(star_twinkle, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(fog_day_density, float_editor(SPEED_FINE, 0.0f, FLT_MAX)),
        INFO_FIELD(fog_night_density, float_editor(SPEED_FINE, 0.0f, FLT_MAX)),
        INFO_FIELD(fog_height_falloff, float_editor(SPEED_FINE, 0.0f, FLT_MAX)),
        INFO_FIELD(fog_base_height, float_editor(SPEED_MEDIUM)),
        INFO_FIELD(fog_anisotropy, float_editor(SPEED_SMALL, -1.0f, 1.0f)),
        INFO_FIELD(fog_max_distance, float_editor(SPEED_HUGE, 0.0f, FLT_MAX)),
        INFO_FIELD(fog_color, color_editor()),
        INFO_FIELD(bottom_radius, float_editor(SPEED_LARGE, 0.0f, FLT_MAX)),
        INFO_FIELD(top_radius, float_editor(SPEED_LARGE, 0.0f, FLT_MAX)),
        INFO_FIELD(rayleigh_scattering, vec3_editor(SPEED_FINE)),
        INFO_FIELD(rayleigh_scale_height, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(mie_scattering, vec3_editor(SPEED_FINE)),
        INFO_FIELD(mie_absorption, vec3_editor(SPEED_FINE)),
        INFO_FIELD(mie_scale_height, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(mie_anisotropy, float_editor(SPEED_SMALL, -1.0f, 1.0f)),
        INFO_FIELD(evening_aerosol, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(ozone_absorption, vec3_editor(SPEED_FINE)),
        INFO_FIELD(ozone_center, float_editor(SPEED_MEDIUM, 0.0f, FLT_MAX)),
        INFO_FIELD(ozone_width, float_editor(SPEED_MEDIUM, 0.0f, FLT_MAX)),
        INFO_FIELD(ground_albedo, color_editor()),
        INFO_FIELD(aerial_max_distance, float_editor(SPEED_MEDIUM, 0.0f, FLT_MAX)),
        INFO_FIELD(clouds_enabled, bool_editor()),
        INFO_FIELD(cloud_bottom, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(cloud_thickness, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(cloud_coverage, float_editor(SPEED_SMALL, 0.0f, 1.0f)),
        INFO_FIELD(cloud_density, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD(cloud_type, float_editor(SPEED_SMALL, 0.0f, 1.0f)),
        INFO_FIELD(cloud_multi_scattering, float_editor(SPEED_SMALL, 0.0f, 1.0f)),
        INFO_FIELD(cloud_powder, float_editor(SPEED_SMALL, 0.0f, FLT_MAX))
    };

    return fields;
}

#undef INFO_FIELD

bool edit_atmosphere(glaze::world::res::atmosphere_create_info& info)
{
    return edit_fields(atmosphere_fields(), info);
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

void draw_model_fields(scene_entity* entity)
{
    component::model* target = find_component<component::model>(entity);

    if (target == nullptr) {
        return;
    }

    std::string path = target->get_path();

    if (!ui::file_field::draw("model", path, MODEL_FILTERS, ui::file_field::pick_mode::file) || path == target->get_path()) {
        return;
    }

    if (!is_model_path(path)) {
        report("model file not found: " + path);
        return;
    }

    try {
        target->set_path(path);
    }

    catch (const std::exception& error) {
        report(error.what());
    }
}

void draw_light_fields(scene_entity* entity)
{
    component::light* target = find_component<component::light>(entity);

    if (target == nullptr) {
        return;
    }

    component::light_create_info info = target->get_create_info();

    if (edit_light(info)) {
        target->set_create_info(info);
    }
}

void draw_plane_fields(scene_entity* entity)
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
        report(error.what());
    }
}

void draw_atmosphere_fields(scene_entity* entity)
{
    glaze::world::res::cmp_atmosphere* target = find_component<glaze::world::res::cmp_atmosphere>(entity);

    if (target == nullptr) {
        return;
    }

    glaze::world::res::atmosphere_create_info info = target->get_create_info();

    if (edit_atmosphere(info)) {
        target->set_create_info(info);
    }
}

void draw_post_fields(scene_entity* entity)
{
    component::post* target = find_component<component::post>(entity);

    if (target == nullptr) {
        return;
    }

    component::post_create_info info = target->get_create_info();

    if (edit_post(info)) {
        target->set_create_info(info);
    }
}

void draw_camera_fields(scene_entity* entity)
{
    scene::debug_camera_player_obj* target = dynamic_cast<scene::debug_camera_player_obj*>(entity);

    if (target == nullptr) {
        return;
    }

    float speed = target->get_speed();

    if (edit_float("speed", speed, SPEED_MEDIUM, 0.0f, FLT_MAX)) {
        target->set_speed(speed);
    }
}

scene_entity* create_model(glm::vec3 position)
{
    if (!is_model_path(m_model_draft)) {
        report("model file not found: " + m_model_draft);
        return nullptr;
    }

    try {
        return glaze::scene::it::create_entity<glaze::scene::obj::static_object_model_prim>(position, glm::vec3(0.0f), glm::vec3(1.0f), m_model_draft);
    }

    catch (const std::exception& error) {
        report(error.what());
        return nullptr;
    }
}

scene_entity* create_plane(glm::vec3 position)
{
    if (!is_pack_path(m_plane_draft.pack_path)) {
        report("texture pack folder not found: " + m_plane_draft.pack_path);
        return nullptr;
    }

    glaze::world::res::textured_plane_create_info info = m_plane_draft;
    info.position = position;

    try {
        return glaze::scene::it::create_entity<glaze::world::res::textured_plane>(info);
    }

    catch (const std::exception& error) {
        report(error.what());
        return nullptr;
    }
}

std::vector<object_type> build_types()
{
    std::vector<object_type> types;

    object_type model;
    model.name          = "static_object_model_prim";
    model.label         = "Static Mesh";
    model.is_creatable  = true;
    model.matches       = is_type<glaze::scene::obj::static_object_model_prim>;
    model.draw_fields   = draw_model_fields;
    model.draw_draft    = [] { ui::file_field::draw("model", m_model_draft, MODEL_FILTERS, ui::file_field::pick_mode::file); };
    model.create        = create_model;
    types.push_back(model);

    object_type plane;
    plane.name         = "textured_plane";
    plane.label        = "Textured Plane";
    plane.is_creatable = true;
    plane.matches      = is_type<glaze::world::res::textured_plane>;
    plane.draw_fields  = draw_plane_fields;
    plane.draw_draft   = [] { edit_plane(m_plane_draft, PLANE_DRAFT_PREVIEW); };
    plane.create       = create_plane;
    types.push_back(plane);

    object_type light;
    light.name         = "light_object_prim";
    light.label        = "Light";
    light.is_creatable = true;
    light.matches      = is_type<glaze::scene::obj::light_object_prim>;
    light.draw_fields  = draw_light_fields;
    light.draw_draft   = [] { edit_light(m_light_draft); };
    light.create       = [](glm::vec3 position) -> scene_entity* {
        return glaze::scene::it::create_entity<glaze::scene::obj::light_object_prim>(position, glm::vec3(0.0f), m_light_draft);
    };
    types.push_back(light);

    object_type sky;
    sky.name          = "sky_atmosphere";
    sky.label         = "Sky Atmosphere";
    sky.has_transform     = false;
    sky.is_creatable      = true;
    sky.only_first_active = true;
    sky.matches       = is_type<glaze::world::res::sky_atmosphere>;
    sky.draw_fields   = draw_atmosphere_fields;
    sky.draw_draft    = [] { edit_atmosphere(m_atmosphere_draft); };
    sky.create        = [](glm::vec3) -> scene_entity* {
        return glaze::scene::it::create_entity<glaze::world::res::sky_atmosphere>(m_atmosphere_draft);
    };
    types.push_back(sky);

    object_type post;
    post.name          = "post_object_prim";
    post.label         = "Post Process";
    post.has_transform     = false;
    post.is_creatable      = true;
    post.only_first_active = true;
    post.matches       = is_type<glaze::scene::obj::post_object_prim>;
    post.draw_fields   = draw_post_fields;
    post.draw_draft    = [] { edit_post(m_post_draft); };
    post.create        = [](glm::vec3) -> scene_entity* {
        return glaze::scene::it::create_entity<glaze::scene::obj::post_object_prim>(m_post_draft);
    };
    types.push_back(post);

    object_type debug_camera;
    debug_camera.name        = "debug_camera_player_obj";
    debug_camera.label       = "Debug Camera";
    debug_camera.matches     = is_type<scene::debug_camera_player_obj>;
    debug_camera.draw_fields = draw_camera_fields;
    types.push_back(debug_camera);

    object_type static_camera;
    static_camera.name        = "static_camera_object_prim";
    static_camera.label       = "Static Camera";
    static_camera.matches     = is_type<glaze::scene::obj::static_camera_object_prim>;
    types.push_back(static_camera);

    object_type debug_grid;
    debug_grid.name          = "debug_grid_object";
    debug_grid.label         = "Debug Grid";
    debug_grid.has_transform = false;
    debug_grid.matches       = is_type<scene::debug_grid_object>;
    types.push_back(debug_grid);

    object_type skybox;
    skybox.name          = "skybox";
    skybox.label         = "Skybox";
    skybox.has_transform = false;
    skybox.matches       = is_type<glaze::world::res::skybox>;
    types.push_back(skybox);

    return types;
}

}

const std::vector<object_type>& get_types()
{
    static const std::vector<object_type> types = build_types();

    return types;
}

const object_type* find_type(scene_entity* entity)
{
    if (entity == nullptr) {
        return nullptr;
    }

    for (const object_type& type : get_types()) {
        if (type.matches(entity)) {
            return &type;
        }
    }

    return nullptr;
}

std::string get_entity_name(scene_entity* entity)
{
    const object_type* type = find_type(entity);

    return std::string(type != nullptr ? type->name : UNKNOWN_TYPE_NAME) + "_" + std::to_string(entity->get_id());
}

const std::string& get_last_error()
{
    return m_last_error;
}

void clear_last_error()
{
    m_last_error.clear();
}

}

#endif
