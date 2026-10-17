#pragma once

#include "glp/glaze/scene/scene.h"
#include "glp/glaze/scene/volume/volume.h"
#include "glp/glaze/scene/volume/cmp_light.h"

#include "glp/glaze/rendering/scene_render/resources/gpu_atmosphere.h"

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

    float bottom_radius = 6360.0f;
    float top_radius    = 6460.0f;

    glm::vec3 rayleigh_scattering = glm::vec3(0.005802f, 0.013558f, 0.0331f);
    float rayleigh_scale_height   = 8.0f;

    glm::vec3 mie_scattering = glm::vec3(0.003996f);
    glm::vec3 mie_absorption = glm::vec3(0.00444f);
    float mie_scale_height   = 1.2f;
    float mie_anisotropy     = 0.8f;

    glm::vec3 ozone_absorption = glm::vec3(0.00065f, 0.001881f, 0.000085f);
    float ozone_center         = 25.0f;
    float ozone_width          = 15.0f;

    glm::vec3 ground_albedo = glm::vec3(0.3f);

    float aerial_max_distance = 32.0f;

    bool clouds_enabled = true;

    float cloud_bottom    = 3.375f;
    float cloud_thickness = 10.0f;
    float cloud_coverage  = 0.5f;
    float cloud_density   = 1.0f;
    float cloud_type      = 0.7f;
};

class cmp_atmosphere : public scene::vol::volume_component
{
public:
    cmp_atmosphere(const atmosphere_create_info& create_info, scene::vol::component::light* sun);

    void update() override;

    rendering::scene_render::gpu_atmosphere get_atmosphere() override;

    void set_time_of_day(float time_of_day) { m_create_info.time_of_day = time_of_day; }
    float get_time_of_day() const           { return m_create_info.time_of_day; }

private:
    atmosphere_create_info m_create_info;

    scene::vol::component::light* m_sun = nullptr;

    glm::vec3 m_sun_direction = glm::vec3(0.0f, 1.0f, 0.0f);
};

class sky_atmosphere : public scene::scene_entity
{
public:
    sky_atmosphere(const atmosphere_create_info& create_info = {});
};

}
