#ifndef NDEBUG

#include "glp/trainer/objects/object_types.h"

#include "glp/trainer/scene/debug_camera.h"
#include "glp/trainer/scene/debug_grid.h"
#include "glp/trainer/ui/file_field.h"

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
#include <exception>
#include <filesystem>

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
    return ImGui::DragFloat(label, &value, speed, min, max);
}

bool edit_vec2(const char* label, glm::vec2& value, float speed)
{
    return ImGui::DragFloat2(label, &value.x, speed);
}

bool edit_vec3(const char* label, glm::vec3& value, float speed)
{
    return ImGui::DragFloat3(label, &value.x, speed, 0.0f, 0.0f, "%.6f");
}

bool edit_color(const char* label, glm::vec3& value)
{
    return ImGui::ColorEdit3(label, &value.x, ImGuiColorEditFlags_Float);
}

bool edit_uint(const char* label, uint32_t& value, uint32_t min, uint32_t max)
{
    return ImGui::DragScalar(label, ImGuiDataType_U32, &value, 1.0f, &min, &max);
}

template<typename enum_type, size_t count>
bool edit_enum(const char* label, enum_type& value, const std::array<const char*, count>& names)
{
    int index = static_cast<int>(value);

    if (!ImGui::Combo(label, &index, names.data(), static_cast<int>(count))) {
        return false;
    }

    value = static_cast<enum_type>(index);

    return true;
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

bool edit_post(component::post_create_info& info)
{
    bool changed = false;

    ImGui::SeparatorText("exposure");

    changed |= edit_float("ev100", info.ev100, SPEED_SMALL);
    changed |= ImGui::Checkbox("auto exposure", &info.auto_exposure);
    changed |= edit_float("compensation", info.exposure_compensation, SPEED_SMALL);
    changed |= edit_float("min ev100", info.min_ev100, SPEED_SMALL);
    changed |= edit_float("max ev100", info.max_ev100, SPEED_SMALL);
    changed |= edit_float("speed up", info.exposure_speed_up, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("speed down", info.exposure_speed_down, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("night darkening", info.exposure_night_darkening, SPEED_SMALL, 0.0f, 1.0f);
    changed |= edit_float("center weight", info.exposure_center_weight, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= ImGui::Checkbox("incident metering", &info.exposure_incident_metering);

    ImGui::SeparatorText("tonemap");

    changed |= edit_enum("tonemapper", info.tonemapper, TONEMAPPERS);
    changed |= edit_float("contrast", info.tonemap_contrast, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("saturation", info.tonemap_saturation, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("sharpen", info.sharpen, SPEED_SMALL, 0.0f, 1.0f);

    ImGui::SeparatorText("effects");

    changed |= edit_float("bloom", info.bloom_intensity, SPEED_FINE, 0.0f, FLT_MAX);
    changed |= edit_float("light shafts", info.light_shafts_intensity, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("sun glare", info.sun_glare_intensity, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("night vision", info.night_vision_intensity, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("gtao radius", info.gtao_radius, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("gtao intensity", info.gtao_intensity, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_uint("gtao directions", info.gtao_directions, 1, MAX_GTAO_DIRECTIONS);
    changed |= edit_uint("gtao steps", info.gtao_steps, 1, MAX_GTAO_STEPS);

    ImGui::SeparatorText("rendering");

    changed |= edit_enum("upscale", info.upscale_mode, UPSCALE_MODES);
    changed |= edit_float("gpu budget", info.gpu_budget_fraction, SPEED_SMALL, 0.0f, 1.0f);
    changed |= ImGui::Checkbox("shadows", &info.shadows_enabled);
    changed |= edit_float("shadow lod bias", info.shadow_lod_bias, SPEED_SMALL);
    changed |= edit_float("shadow softness", info.shadow_softness, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("shadow normal bias", info.shadow_normal_bias, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("contact shadows", info.contact_shadow_length, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= ImGui::Checkbox("volumetric fog", &info.volumetric_fog_enabled);
    changed |= edit_float("fog distance", info.volumetric_fog_distance, SPEED_LARGE, 0.0f, FLT_MAX);

    return changed;
}

bool edit_atmosphere(glaze::world::res::atmosphere_create_info& info)
{
    bool changed = false;

    ImGui::SeparatorText("sun");

    changed |= edit_float("time of day", info.time_of_day, SPEED_SMALL, 0.0f, MAX_HOURS);
    changed |= edit_float("max elevation", info.sun_max_elevation, SPEED_MEDIUM, 0.0f, MAX_ELEVATION);
    changed |= edit_float("sun illuminance", info.sun_illuminance, SPEED_HUGE, 0.0f, FLT_MAX);
    changed |= edit_float("sun angular radius", info.sun_angular_radius, SPEED_FINE, 0.0f, MAX_ELEVATION);
    changed |= edit_float("sun light scale", info.sun_light_scale, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("sun temperature", info.sun_temperature, SPEED_HUGE, 0.0f, FLT_MAX);
    changed |= edit_float("horizon fade", info.horizon_light_fade, SPEED_MEDIUM, 0.0f, FLT_MAX);
    changed |= edit_float("horizon twilight", info.horizon_light_twilight, SPEED_MEDIUM, 0.0f, FLT_MAX);
    changed |= edit_float("horizon desaturation", info.horizon_light_desaturation, SPEED_SMALL, 0.0f, 1.0f);
    changed |= edit_float("evening sun pink", info.evening_sun_pink, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("evening halo pink", info.evening_halo_pink, SPEED_SMALL, 0.0f, FLT_MAX);

    ImGui::SeparatorText("moon");

    changed |= edit_float("moon illuminance", info.moon_illuminance, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("moon angular radius", info.moon_angular_radius, SPEED_FINE, 0.0f, MAX_ELEVATION);
    changed |= edit_float("moon light scale", info.moon_light_scale, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("moon overlap hours", info.moon_overlap_hours, SPEED_SMALL, 0.0f, MAX_HOURS);
    changed |= edit_color("moon color", info.moon_color);
    changed |= edit_float("moon flare size", info.moon_flare_size, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("moon flare falloff", info.moon_flare_falloff, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("moon flare multiplier", info.moon_flare_multiplier, SPEED_FINE, 0.0f, FLT_MAX);
    changed |= edit_color("moon flare tint", info.moon_flare_tint);

    ImGui::SeparatorText("sky");

    changed |= edit_float("night ambient scale", info.night_ambient_scale, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("day sky scale", info.day_sky_scale, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("twilight desaturation", info.twilight_desaturation, SPEED_SMALL, 0.0f, 1.0f);
    changed |= edit_float("night sky luminance", info.night_sky_luminance, SPEED_FINE, 0.0f, FLT_MAX);
    changed |= edit_float("star intensity", info.star_intensity, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("star twinkle", info.star_twinkle, SPEED_SMALL, 0.0f, FLT_MAX);

    ImGui::SeparatorText("fog");

    changed |= edit_float("fog day density", info.fog_day_density, SPEED_FINE, 0.0f, FLT_MAX);
    changed |= edit_float("fog night density", info.fog_night_density, SPEED_FINE, 0.0f, FLT_MAX);
    changed |= edit_float("fog height falloff", info.fog_height_falloff, SPEED_FINE, 0.0f, FLT_MAX);
    changed |= edit_float("fog base height", info.fog_base_height, SPEED_MEDIUM);
    changed |= edit_float("fog anisotropy", info.fog_anisotropy, SPEED_SMALL, -1.0f, 1.0f);
    changed |= edit_float("fog max distance", info.fog_max_distance, SPEED_HUGE, 0.0f, FLT_MAX);
    changed |= edit_color("fog color", info.fog_color);

    ImGui::SeparatorText("atmosphere");

    changed |= edit_float("bottom radius", info.bottom_radius, SPEED_LARGE, 0.0f, FLT_MAX);
    changed |= edit_float("top radius", info.top_radius, SPEED_LARGE, 0.0f, FLT_MAX);
    changed |= edit_vec3("rayleigh scattering", info.rayleigh_scattering, SPEED_FINE);
    changed |= edit_float("rayleigh scale height", info.rayleigh_scale_height, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_vec3("mie scattering", info.mie_scattering, SPEED_FINE);
    changed |= edit_vec3("mie absorption", info.mie_absorption, SPEED_FINE);
    changed |= edit_float("mie scale height", info.mie_scale_height, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("mie anisotropy", info.mie_anisotropy, SPEED_SMALL, -1.0f, 1.0f);
    changed |= edit_float("evening aerosol", info.evening_aerosol, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_vec3("ozone absorption", info.ozone_absorption, SPEED_FINE);
    changed |= edit_float("ozone center", info.ozone_center, SPEED_MEDIUM, 0.0f, FLT_MAX);
    changed |= edit_float("ozone width", info.ozone_width, SPEED_MEDIUM, 0.0f, FLT_MAX);
    changed |= edit_color("ground albedo", info.ground_albedo);
    changed |= edit_float("aerial max distance", info.aerial_max_distance, SPEED_MEDIUM, 0.0f, FLT_MAX);

    ImGui::SeparatorText("clouds");

    changed |= ImGui::Checkbox("clouds", &info.clouds_enabled);
    changed |= edit_float("cloud bottom", info.cloud_bottom, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("cloud thickness", info.cloud_thickness, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("cloud coverage", info.cloud_coverage, SPEED_SMALL, 0.0f, 1.0f);
    changed |= edit_float("cloud density", info.cloud_density, SPEED_SMALL, 0.0f, FLT_MAX);
    changed |= edit_float("cloud type", info.cloud_type, SPEED_SMALL, 0.0f, 1.0f);
    changed |= edit_float("cloud multi scattering", info.cloud_multi_scattering, SPEED_SMALL, 0.0f, 1.0f);
    changed |= edit_float("cloud powder", info.cloud_powder, SPEED_SMALL, 0.0f, FLT_MAX);

    return changed;
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

    committed |= ImGui::Checkbox("flip normal y", &info.flip_normal_y);

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

void draw_nothing(scene_entity*)
{
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
    model.is_creatable  = true;
    model.matches       = is_type<glaze::scene::obj::static_object_model_prim>;
    model.draw_fields   = draw_model_fields;
    model.draw_draft    = [] { ui::file_field::draw("model", m_model_draft, MODEL_FILTERS, ui::file_field::pick_mode::file); };
    model.create        = create_model;
    types.push_back(model);

    object_type plane;
    plane.name         = "textured_plane";
    plane.is_creatable = true;
    plane.matches      = is_type<glaze::world::res::textured_plane>;
    plane.draw_fields  = draw_plane_fields;
    plane.draw_draft   = [] { edit_plane(m_plane_draft, PLANE_DRAFT_PREVIEW); };
    plane.create       = create_plane;
    types.push_back(plane);

    object_type light;
    light.name         = "light_object_prim";
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
    debug_camera.matches     = is_type<scene::debug_camera_player_obj>;
    debug_camera.draw_fields = draw_camera_fields;
    types.push_back(debug_camera);

    object_type static_camera;
    static_camera.name        = "static_camera_object_prim";
    static_camera.matches     = is_type<glaze::scene::obj::static_camera_object_prim>;
    static_camera.draw_fields = draw_nothing;
    types.push_back(static_camera);

    object_type debug_grid;
    debug_grid.name          = "debug_grid_object";
    debug_grid.has_transform = false;
    debug_grid.matches       = is_type<scene::debug_grid_object>;
    debug_grid.draw_fields   = draw_nothing;
    types.push_back(debug_grid);

    object_type skybox;
    skybox.name          = "skybox";
    skybox.has_transform = false;
    skybox.matches       = is_type<glaze::world::res::skybox>;
    skybox.draw_fields   = draw_nothing;
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
