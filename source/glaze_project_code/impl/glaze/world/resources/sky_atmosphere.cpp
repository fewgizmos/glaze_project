#include "glp/glaze/world/resources/sky_atmosphere.h"

#include <glm/common.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace glaze::world::res
{

namespace
{

constexpr float SUNRISE_HOUR   = 6.0f;
constexpr float DAYLIGHT_HOURS = 12.0f;
constexpr float DAY_HOURS      = 24.0f;
constexpr float NOON_HOUR      = 12.0f;
constexpr float SUNSET_HOUR    = 18.0f;

constexpr float MAX_MOON_OVERLAP_HOURS = 5.0f;

constexpr float HORIZON_FADE_START = -0.5f;

constexpr float PLANET_SURFACE_OFFSET = 0.001f;

constexpr int TRANSMITTANCE_SAMPLES = 40;

constexpr float MOON_LIGHT_ANGULAR_RADIUS = 0.26f;

const glm::vec3 LUMINANCE_WEIGHTS = glm::vec3(0.2126f, 0.7152f, 0.0722f);

float ray_sphere_nearest(glm::vec3 origin, glm::vec3 direction, float radius)
{
    const float b = glm::dot(origin, direction);
    const float c = glm::dot(origin, origin) - radius * radius;

    const float discriminant = b * b - c;

    if (discriminant < 0.0f) {
        return -1.0f;
    }

    const float root = std::sqrt(discriminant);

    const float near_t = -b - root;
    const float far_t  = -b + root;

    if (far_t < 0.0f) {
        return -1.0f;
    }

    return near_t >= 0.0f ? near_t : far_t;
}

glm::vec3 light_transmittance(const atmosphere_create_info& info, glm::vec3 direction)
{
    const glm::vec3 origin(0.0f, info.bottom_radius + PLANET_SURFACE_OFFSET, 0.0f);

    if (ray_sphere_nearest(origin, direction, info.bottom_radius) >= 0.0f) {
        return glm::vec3(0.0f);
    }

    const float end_t = std::max(ray_sphere_nearest(origin, direction, info.top_radius), 0.0f);
    const float dt    = end_t / static_cast<float>(TRANSMITTANCE_SAMPLES);

    const glm::vec3 mie_extinction = info.mie_scattering + info.mie_absorption;

    glm::vec3 optical_depth(0.0f);

    for (int i = 0; i < TRANSMITTANCE_SAMPLES; i++) {
        const glm::vec3 position = origin + direction * ((static_cast<float>(i) + 0.5f) * dt);

        const float height = glm::length(position) - info.bottom_radius;

        const float rayleigh_density = std::exp(-height / info.rayleigh_scale_height);
        const float mie_density      = std::exp(-height / info.mie_scale_height);
        const float ozone_density    = std::max(0.0f, 1.0f - std::abs(height - info.ozone_center) / info.ozone_width);

        optical_depth += (info.rayleigh_scattering * rayleigh_density + mie_extinction * mie_density + info.ozone_absorption * ozone_density) * dt;
    }

    return glm::exp(-optical_depth);
}

glm::vec3 arc_direction(float hour, float rise_hour, float visible_hours, float max_elevation)
{
    const float since_rise = std::fmod(std::fmod(hour - rise_hour, DAY_HOURS) + DAY_HOURS, DAY_HOURS);

    const float angle = since_rise <= visible_hours
        ? since_rise / visible_hours * glm::pi<float>()
        : glm::pi<float>() * (1.0f + (since_rise - visible_hours) / (DAY_HOURS - visible_hours));

    const float elevation = glm::radians(max_elevation);

    return glm::normalize(glm::vec3(
        std::cos(angle),
        std::sin(angle) * std::sin(elevation),
        -std::sin(angle) * std::cos(elevation)
    ));
}

float horizon_factor(glm::vec3 direction, float fade_degrees)
{
    if (fade_degrees <= 0.0f) {
        return 1.0f;
    }

    const float elevation = glm::degrees(std::asin(std::clamp(direction.y, -1.0f, 1.0f)));

    return glm::smoothstep(HORIZON_FADE_START, fade_degrees, elevation);
}

glm::vec3 horizon_desaturate(glm::vec3 color, float amount, float horizon)
{
    return glm::mix(color, glm::vec3(glm::dot(color, LUMINANCE_WEIGHTS)), amount * (1.0f - horizon));
}

float evening_factor(float time_of_day)
{
    const float hour = std::fmod(std::fmod(time_of_day, DAY_HOURS) + DAY_HOURS, DAY_HOURS);

    return glm::smoothstep(NOON_HOUR, SUNSET_HOUR, hour);
}

glm::vec3 evening_tint(float time_of_day, float evening_pink)
{
    const float pink = glm::mix(1.0f, evening_pink, evening_factor(time_of_day));

    const glm::vec3 tint(1.0f, 1.0f, pink);

    return tint / glm::dot(tint, LUMINANCE_WEIGHTS);
}

atmosphere_create_info with_evening_aerosol(const atmosphere_create_info& info)
{
    const float scale = glm::mix(1.0f, info.evening_aerosol, evening_factor(info.time_of_day));

    atmosphere_create_info result = info;

    result.mie_scattering   *= scale;
    result.mie_absorption   *= scale;
    result.mie_scale_height *= scale;

    return result;
}

glm::mat4 celestial_rotation(float time_of_day, float max_elevation)
{
    const float latitude = glm::radians(90.0f - max_elevation);

    const glm::vec3 east(1.0f, 0.0f, 0.0f);
    const glm::vec3 pole(0.0f, std::sin(latitude), std::cos(latitude));
    const glm::vec3 meridian = glm::cross(pole, east);

    const glm::mat3 to_equatorial = glm::transpose(glm::mat3(east, meridian, pole));

    const float hour_angle = time_of_day / DAY_HOURS * glm::two_pi<float>();

    return glm::rotate(glm::mat4(1.0f), -hour_angle, glm::vec3(0.0f, 0.0f, 1.0f)) * glm::mat4(to_equatorial);
}

void orient_light(scene::vol::component::light* light, glm::vec3 direction, glm::vec3 color, float intensity)
{
    const glm::vec3 forward = -direction;

    light->set_rot(glm::vec3(
        glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f))),
        glm::degrees(std::atan2(-forward.x, -forward.z)),
        0.0f
    ));

    light->set_color(color);
    light->set_intensity(intensity);
}

}

cmp_atmosphere::cmp_atmosphere(const atmosphere_create_info& create_info, scene::vol::component::light* sun, scene::vol::component::light* moon)
    : m_create_info(create_info), m_sun(sun), m_moon(moon)
{
    set_flag(scene::vol::component_flags::atmosphere);

    update();
}

void cmp_atmosphere::update()
{
    const float overlap = std::clamp(m_create_info.moon_overlap_hours, 0.0f, MAX_MOON_OVERLAP_HOURS);

    m_sun_direction  = arc_direction(m_create_info.time_of_day, SUNRISE_HOUR, DAYLIGHT_HOURS, m_create_info.sun_max_elevation);
    m_moon_direction = arc_direction(m_create_info.time_of_day, SUNSET_HOUR - overlap, DAYLIGHT_HOURS + 2.0f * overlap, m_create_info.sun_max_elevation);

    m_celestial_rotation = celestial_rotation(m_create_info.time_of_day, m_create_info.sun_max_elevation);

    if (m_sun != nullptr) {
        const float horizon = horizon_factor(m_sun_direction, m_create_info.horizon_light_fade);

        const glm::vec3 color = evening_tint(m_create_info.time_of_day, m_create_info.evening_sun_pink) * light_transmittance(with_evening_aerosol(m_create_info), m_sun_direction);

        orient_light(
            m_sun,
            m_sun_direction,
            horizon_desaturate(color, m_create_info.horizon_light_desaturation, horizon),
            m_create_info.sun_illuminance * m_create_info.sun_light_scale * horizon
        );
    }

    if (m_moon != nullptr) {
        const float horizon = horizon_factor(m_moon_direction, m_create_info.horizon_light_fade);

        const glm::vec3 color = m_create_info.moon_color * light_transmittance(with_evening_aerosol(m_create_info), m_moon_direction);

        orient_light(
            m_moon,
            m_moon_direction,
            horizon_desaturate(color, m_create_info.horizon_light_desaturation, horizon),
            m_create_info.moon_illuminance * m_create_info.moon_light_scale * horizon
        );
    }
}

rendering::scene_render::gpu_atmosphere cmp_atmosphere::get_atmosphere()
{
    rendering::scene_render::gpu_atmosphere result;

    result.sun_direction      = m_sun_direction;
    result.sun_illuminance    = m_create_info.sun_illuminance;
    result.sun_angular_radius = m_create_info.sun_angular_radius;
    result.sun_tint           = evening_tint(m_create_info.time_of_day, m_create_info.evening_halo_pink);

    result.moon_direction = m_moon_direction;
    result.moon_color     = m_create_info.moon_color;

    result.moon_illuminance    = m_create_info.moon_illuminance;
    result.moon_angular_radius = m_create_info.moon_angular_radius;

    result.moon_flare_size       = m_create_info.moon_flare_size;
    result.moon_flare_falloff    = m_create_info.moon_flare_falloff;
    result.moon_flare_multiplier = m_create_info.moon_flare_multiplier;
    result.moon_flare_tint       = m_create_info.moon_flare_tint;

    result.night_ambient_scale   = m_create_info.night_ambient_scale;
    result.twilight_desaturation = m_create_info.twilight_desaturation;
    result.night_sky_luminance = m_create_info.night_sky_luminance;
    result.star_intensity      = m_create_info.star_intensity;
    result.star_twinkle        = m_create_info.star_twinkle;

    result.celestial_rotation = m_celestial_rotation;

    result.bottom_radius = m_create_info.bottom_radius;
    result.top_radius    = m_create_info.top_radius;

    result.rayleigh_scattering   = m_create_info.rayleigh_scattering;
    result.rayleigh_scale_height = m_create_info.rayleigh_scale_height;

    const atmosphere_create_info medium = with_evening_aerosol(m_create_info);

    result.mie_scattering   = medium.mie_scattering;
    result.mie_absorption   = medium.mie_absorption;
    result.mie_scale_height = medium.mie_scale_height;
    result.mie_anisotropy   = m_create_info.mie_anisotropy;

    result.ozone_absorption = m_create_info.ozone_absorption;
    result.ozone_center     = m_create_info.ozone_center;
    result.ozone_width      = m_create_info.ozone_width;

    result.ground_albedo = m_create_info.ground_albedo;

    result.aerial_max_distance = m_create_info.aerial_max_distance;

    result.clouds_enabled = m_create_info.clouds_enabled;

    result.cloud_bottom    = m_create_info.cloud_bottom;
    result.cloud_thickness = m_create_info.cloud_thickness;
    result.cloud_coverage  = m_create_info.cloud_coverage;
    result.cloud_density   = m_create_info.cloud_density;
    result.cloud_type      = m_create_info.cloud_type;

    result.cloud_multi_scattering = m_create_info.cloud_multi_scattering;
    result.cloud_powder           = m_create_info.cloud_powder;

    return result;
}

sky_atmosphere::sky_atmosphere(const atmosphere_create_info& create_info)
{
    scene::vol::component::light_create_info sun_info;
    sun_info.type           = scene::vol::component::light_type::directional;
    sun_info.intensity      = create_info.sun_illuminance;
    sun_info.angular_radius = create_info.sun_angular_radius;

    scene::vol::component::light_create_info moon_info;
    moon_info.type           = scene::vol::component::light_type::directional;
    moon_info.intensity      = create_info.moon_illuminance;
    moon_info.angular_radius = MOON_LIGHT_ANGULAR_RADIUS;

    std::unique_ptr<scene::vol::component::light> sun  = std::make_unique<scene::vol::component::light>(sun_info);
    std::unique_ptr<scene::vol::component::light> moon = std::make_unique<scene::vol::component::light>(moon_info);

    scene::vol::component::light* sun_pointer  = sun.get();
    scene::vol::component::light* moon_pointer = moon.get();

    m_scene_volume().components.push_back(std::move(sun));
    m_scene_volume().components.push_back(std::move(moon));
    std::unique_ptr<cmp_atmosphere> atmosphere = std::make_unique<cmp_atmosphere>(create_info, sun_pointer, moon_pointer);

    m_atmosphere = atmosphere.get();

    m_scene_volume().components.push_back(std::move(atmosphere));
}

}
