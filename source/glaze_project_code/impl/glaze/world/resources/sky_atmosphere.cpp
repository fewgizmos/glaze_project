#include "glp/glaze/world/resources/sky_atmosphere.h"

#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>
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

constexpr float PLANET_SURFACE_OFFSET = 0.001f;

constexpr int TRANSMITTANCE_SAMPLES = 40;

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

glm::vec3 sun_transmittance(const atmosphere_create_info& info, glm::vec3 direction)
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

glm::vec3 sun_direction_at(float time_of_day, float max_elevation)
{
    const float angle     = (time_of_day - SUNRISE_HOUR) / DAYLIGHT_HOURS * glm::pi<float>();
    const float elevation = glm::radians(max_elevation);

    return glm::normalize(glm::vec3(
        std::cos(angle),
        std::sin(angle) * std::sin(elevation),
        -std::sin(angle) * std::cos(elevation)
    ));
}

}

cmp_atmosphere::cmp_atmosphere(const atmosphere_create_info& create_info, scene::vol::component::light* sun)
    : m_create_info(create_info), m_sun(sun)
{
    set_flag(scene::vol::component_flags::atmosphere);

    update();
}

void cmp_atmosphere::update()
{
    m_sun_direction = sun_direction_at(m_create_info.time_of_day, m_create_info.sun_max_elevation);

    if (m_sun == nullptr) {
        return;
    }

    const glm::vec3 forward = -m_sun_direction;

    m_sun->set_rot(glm::vec3(
        glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f))),
        glm::degrees(std::atan2(-forward.x, -forward.z)),
        0.0f
    ));

    m_sun->set_color(sun_transmittance(m_create_info, m_sun_direction));
    m_sun->set_intensity(m_create_info.sun_illuminance * m_create_info.sun_light_scale);
}

rendering::scene_render::gpu_atmosphere cmp_atmosphere::get_atmosphere()
{
    rendering::scene_render::gpu_atmosphere result;

    result.sun_direction      = m_sun_direction;
    result.sun_illuminance    = m_create_info.sun_illuminance;
    result.sun_angular_radius = m_create_info.sun_angular_radius;

    result.bottom_radius = m_create_info.bottom_radius;
    result.top_radius    = m_create_info.top_radius;

    result.rayleigh_scattering   = m_create_info.rayleigh_scattering;
    result.rayleigh_scale_height = m_create_info.rayleigh_scale_height;

    result.mie_scattering   = m_create_info.mie_scattering;
    result.mie_absorption   = m_create_info.mie_absorption;
    result.mie_scale_height = m_create_info.mie_scale_height;
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

    return result;
}

sky_atmosphere::sky_atmosphere(const atmosphere_create_info& create_info)
{
    scene::vol::component::light_create_info sun_info;
    sun_info.type           = scene::vol::component::light_type::directional;
    sun_info.intensity      = create_info.sun_illuminance;
    sun_info.angular_radius = create_info.sun_angular_radius;

    std::unique_ptr<scene::vol::component::light> sun = std::make_unique<scene::vol::component::light>(sun_info);

    scene::vol::component::light* sun_pointer = sun.get();

    m_scene_volume().components.push_back(std::move(sun));
    m_scene_volume().components.push_back(std::make_unique<cmp_atmosphere>(create_info, sun_pointer));
}

}
