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
    float padding1;
    float padding2;
};

layout(buffer_reference, std430) readonly buffer atmosphere_buffer
{
    atmosphere_params params;
};

#endif
