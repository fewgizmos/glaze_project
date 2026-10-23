#ifndef NDEBUG

#include "glp/trainer/objects/field_editors.h"
#include "glp/trainer/objects/object_types.h"

#include "glp/glaze/world/resources/sky_atmosphere.h"

#include <cfloat>

namespace trainer::objects
{

namespace
{

constexpr float SPEED_FINE   = 0.001f;
constexpr float SPEED_SMALL  = 0.01f;
constexpr float SPEED_MEDIUM = 0.1f;
constexpr float SPEED_LARGE  = 1.0f;
constexpr float SPEED_HUGE   = 100.0f;

constexpr float MAX_HOURS     = 24.0f;
constexpr float MAX_ELEVATION = 90.0f;

glaze::world::res::atmosphere_create_info m_atmosphere_draft;

const std::vector<info_field<glaze::world::res::atmosphere_create_info>>& atmosphere_fields()
{
    static const std::vector<info_field<glaze::world::res::atmosphere_create_info>> fields = {
        INFO_FIELD(time_of_day, float_editor(SPEED_SMALL, 0.0f, MAX_HOURS)),
        INFO_FIELD_IN("sun", sun_max_elevation, float_editor(SPEED_MEDIUM, 0.0f, MAX_ELEVATION)),
        INFO_FIELD_IN("sun", sun_illuminance, float_editor(SPEED_HUGE, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("sun", sun_angular_radius, float_editor(SPEED_FINE, 0.0f, MAX_ELEVATION)),
        INFO_FIELD_IN("sun", sun_light_scale, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("sun", sun_temperature, float_editor(SPEED_HUGE, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("horizon", horizon_light_fade, float_editor(SPEED_MEDIUM, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("horizon", horizon_light_twilight, float_editor(SPEED_MEDIUM, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("horizon", horizon_light_desaturation, float_editor(SPEED_SMALL, 0.0f, 1.0f)),
        INFO_FIELD_IN("evening", evening_sun_pink, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("evening", evening_halo_pink, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("evening", evening_aerosol, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("moon", moon_illuminance, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("moon", moon_angular_radius, float_editor(SPEED_FINE, 0.0f, MAX_ELEVATION)),
        INFO_FIELD_IN("moon", moon_light_scale, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("moon", moon_overlap_hours, float_editor(SPEED_SMALL, 0.0f, MAX_HOURS)),
        INFO_FIELD_IN("moon", moon_color, color_editor()),
        INFO_FIELD_IN("moon", moon_flare_size, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("moon", moon_flare_falloff, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("moon", moon_flare_multiplier, float_editor(SPEED_FINE, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("moon", moon_flare_tint, color_editor()),
        INFO_FIELD_IN("sky", night_ambient_scale, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("sky", day_sky_scale, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("sky", twilight_desaturation, float_editor(SPEED_SMALL, 0.0f, 1.0f)),
        INFO_FIELD_IN("sky", night_sky_luminance, float_editor(SPEED_FINE, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("stars", star_intensity, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("stars", star_twinkle, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("fog", fog_day_density, float_editor(SPEED_FINE, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("fog", fog_night_density, float_editor(SPEED_FINE, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("fog", fog_height_falloff, float_editor(SPEED_FINE, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("fog", fog_base_height, float_editor(SPEED_MEDIUM)),
        INFO_FIELD_IN("fog", fog_anisotropy, float_editor(SPEED_SMALL, -1.0f, 1.0f)),
        INFO_FIELD_IN("fog", fog_max_distance, float_editor(SPEED_HUGE, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("fog", fog_color, color_editor()),
        INFO_FIELD_IN("planet", bottom_radius, float_editor(SPEED_LARGE, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("planet", top_radius, float_editor(SPEED_LARGE, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("planet", ground_albedo, color_editor()),
        INFO_FIELD_IN("rayleigh", rayleigh_scattering, vec3_editor(SPEED_FINE)),
        INFO_FIELD_IN("rayleigh", rayleigh_scale_height, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("mie", mie_scattering, vec3_editor(SPEED_FINE)),
        INFO_FIELD_IN("mie", mie_absorption, vec3_editor(SPEED_FINE)),
        INFO_FIELD_IN("mie", mie_scale_height, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("mie", mie_anisotropy, float_editor(SPEED_SMALL, -1.0f, 1.0f)),
        INFO_FIELD_IN("ozone", ozone_absorption, vec3_editor(SPEED_FINE)),
        INFO_FIELD_IN("ozone", ozone_center, float_editor(SPEED_MEDIUM, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("ozone", ozone_width, float_editor(SPEED_MEDIUM, 0.0f, FLT_MAX)),
        INFO_FIELD(aerial_max_distance, float_editor(SPEED_MEDIUM, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("clouds", clouds_enabled, bool_editor()),
        INFO_FIELD_IN("clouds", cloud_bottom, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("clouds", cloud_thickness, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("clouds", cloud_coverage, float_editor(SPEED_SMALL, 0.0f, 1.0f)),
        INFO_FIELD_IN("clouds", cloud_density, float_editor(SPEED_SMALL, 0.0f, FLT_MAX)),
        INFO_FIELD_IN("clouds", cloud_type, float_editor(SPEED_SMALL, 0.0f, 1.0f)),
        INFO_FIELD_IN("clouds", cloud_multi_scattering, float_editor(SPEED_SMALL, 0.0f, 1.0f)),
        INFO_FIELD_IN("clouds", cloud_powder, float_editor(SPEED_SMALL, 0.0f, FLT_MAX))
    };

    return fields;
}

void draw_atmosphere_fields(glaze::scene::scene_entity* entity)
{
    glaze::world::res::cmp_atmosphere* target = find_component<glaze::world::res::cmp_atmosphere>(entity);

    if (target == nullptr) {
        return;
    }

    glaze::world::res::atmosphere_create_info info = target->get_create_info();

    if (edit_fields(atmosphere_fields(), info)) {
        target->set_create_info(info);
    }
}

const bool registered = [] {
    object_type type;
    type.name              = "sky_atmosphere";
    type.label             = "Sky Atmosphere";
    type.has_transform     = false;
    type.is_creatable      = true;
    type.only_first_active = true;
    type.matches           = is_type<glaze::world::res::sky_atmosphere>;
    type.draw_fields       = draw_atmosphere_fields;
    type.draw_draft        = [] { edit_fields(atmosphere_fields(), m_atmosphere_draft); };
    type.create            = [](glm::vec3) -> glaze::scene::scene_entity* {
        return glaze::scene::it::create_entity<glaze::world::res::sky_atmosphere>(m_atmosphere_draft);
    };

    register_type(type);

    return true;
}();

}

}

#endif
