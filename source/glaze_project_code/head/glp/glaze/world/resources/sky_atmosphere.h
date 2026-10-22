#pragma once

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/volume/volume.h"
#include "glp/glaze/scene/volume/cmp_light.h"

#include "glp/glaze/rendering/scene_render/resources/gpu_atmosphere.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace glaze::world::res
{

struct atmosphere_create_info
{
    float time_of_day       = 12.0f;
    float sun_max_elevation = 60.0f;

    float sun_illuminance    = 100000.0f;
    float sun_angular_radius = 0.27f;
    float sun_light_scale    = 1.0f;
    float sun_temperature    = 5500.0f;

    float horizon_light_fade         = 0.0f;
    float horizon_light_twilight     = 0.0f;
    float horizon_light_desaturation = 0.0f;

    float evening_sun_pink   = 1.0f;
    float evening_halo_pink  = 1.0f;

    float moon_illuminance    = 0.3f;
    float moon_angular_radius = 0.26f;
    float moon_light_scale    = 1.0f;
    float moon_overlap_hours  = 0.0f;

    glm::vec3 moon_color = glm::vec3(1.0f, 0.94f, 0.86f);

    float moon_flare_size       = 2.0f;
    float moon_flare_falloff    = 4.0f;
    float moon_flare_multiplier = 0.1f;

    glm::vec3 moon_flare_tint = glm::vec3(1.0f);

    float night_ambient_scale   = 1.0f;
    float day_sky_scale         = 1.0f;
    float twilight_desaturation = 0.0f;

    float fog_day_density    = 0.0f;
    float fog_night_density  = 0.0f;
    float fog_height_falloff = 0.01f;
    float fog_base_height    = 0.0f;
    float fog_anisotropy     = 0.6f;
    float fog_max_distance   = 30000.0f;

    glm::vec3 fog_color = glm::vec3(1.0f);
    float night_sky_luminance = 0.0002f;
    float star_intensity      = 1.0f;
    float star_twinkle        = 1.0f;

    float bottom_radius = 6360.0f;
    float top_radius    = 6460.0f;

    glm::vec3 rayleigh_scattering = glm::vec3(0.005802f, 0.013558f, 0.0331f);
    float rayleigh_scale_height   = 8.0f;

    glm::vec3 mie_scattering = glm::vec3(0.003996f);
    glm::vec3 mie_absorption = glm::vec3(0.00444f);
    float mie_scale_height   = 1.2f;
    float mie_anisotropy     = 0.8f;
    float evening_aerosol    = 1.0f;

    glm::vec3 ozone_absorption = glm::vec3(0.00065f, 0.001881f, 0.000085f);
    float ozone_center         = 25.0f;
    float ozone_width          = 15.0f;

    glm::vec3 ground_albedo = glm::vec3(0.3f);

    float aerial_max_distance = 32.0f;

    bool clouds_enabled = true;

    float cloud_bottom    = 10.125f;
    float cloud_thickness = 14.0f;
    float cloud_coverage  = 0.5f;
    float cloud_density   = 1.0f;
    float cloud_type      = 0.7f;

    float cloud_multi_scattering = 0.7f;
    float cloud_powder           = 0.25f;
};

class cmp_atmosphere : public scene::vol::volume_component
{
public:
    cmp_atmosphere(const atmosphere_create_info& create_info, scene::vol::component::light* sun, scene::vol::component::light* moon);

    void update() override;

    rendering::scene_render::gpu_atmosphere get_atmosphere() override;

    void set_time_of_day(float time_of_day) { m_create_info.time_of_day = time_of_day; }
    float get_time_of_day() const           { return m_create_info.time_of_day; }

    const atmosphere_create_info& get_create_info() const { return m_create_info; }
    void set_create_info(const atmosphere_create_info& create_info) { m_create_info = create_info; }

private:
    atmosphere_create_info m_create_info;

    scene::vol::component::light* m_sun  = nullptr;
    scene::vol::component::light* m_moon = nullptr;

    glm::vec3 m_sun_direction  = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 m_moon_direction = glm::vec3(0.0f, -1.0f, 0.0f);

    glm::vec3 m_sun_light  = glm::vec3(0.0f);
    glm::vec3 m_moon_light = glm::vec3(0.0f);

    glm::mat4 m_celestial_rotation = glm::mat4(1.0f);

    glm::vec2 m_cloud_offset = glm::vec2(0.0f);
};

class sky_atmosphere : public scene::scene_entity
{
public:
    sky_atmosphere(const atmosphere_create_info& create_info = {});

    void set_time_of_day(float time_of_day) { m_atmosphere->set_time_of_day(time_of_day); }
    float get_time_of_day() const           { return m_atmosphere->get_time_of_day(); }

private:
    cmp_atmosphere* m_atmosphere = nullptr;
};

}
