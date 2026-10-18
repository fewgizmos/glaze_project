#ifndef ATMOSPHERE_PARAMS_GLSL
#define ATMOSPHERE_PARAMS_GLSL

struct atmosphere_params
{
    mat4 inv_view_proj;

    vec3 camera_position;
    float bottom_radius;

    vec3 sun_direction;
    float top_radius;

    vec3 rayleigh_scattering;
    float rayleigh_exp_scale;

    vec3 mie_scattering;
    float mie_exp_scale;

    vec3 mie_extinction;
    float mie_phase_g;

    vec3 absorption_extinction;
    float absorption_center;

    vec3 ground_albedo;
    float absorption_width;

    float sun_illuminance;
    float sun_angular_radius;
    float aerial_max_distance;
    float padding0;

    mat4 previous_view_proj;

    vec3 camera_world_position;
    uint frame_index;

    float cloud_bottom;
    float cloud_top;
    float cloud_coverage;
    float cloud_density;

    float cloud_type;
    uint clouds_enabled;
    float cloud_luminance_scale;
    float time;

    vec3 moon_direction;
    float moon_illuminance;

    vec3 moon_color;
    float moon_angular_radius;

    mat4 celestial_rotation;

    float night_sky_luminance;
    float star_intensity;
    float star_twinkle;
    float night_ambient_scale;

    float cloud_multi_scattering;
    float cloud_powder;
    float moon_flare_size;
    float moon_flare_falloff;

    vec3 moon_flare_tint;
    float moon_flare_multiplier;

    vec3 sun_tint;
    float twilight_desaturation;

    vec3 fog_sun_illuminance;
    float fog_density;

    vec3 fog_moon_illuminance;
    float fog_height_falloff;

    vec3 fog_color;
    float fog_base_height;

    float fog_anisotropy;
    float fog_max_distance;
    float padding1;
    float padding2;
};

layout(buffer_reference, std430) readonly buffer atmosphere_buffer
{
    atmosphere_params params;
};

#endif
