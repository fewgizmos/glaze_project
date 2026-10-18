#version 450
#extension GL_EXT_buffer_reference : require

#include "head/scene.glsl"
#include "head/night_sky.glsl"
#include "head/height_fog.glsl"

layout(set = 1, binding = 0) uniform sampler2D transmittance_lut;
layout(set = 1, binding = 2) uniform sampler2DArray sky_view_lut;
layout(set = 1, binding = 4) uniform sampler2D clouds;

layout(location = 0) in vec3 view_ray;
layout(location = 1) in vec2 screen_uv;

layout(location = 0) out vec4 out_color;

const float MAX_HDR_VALUE = 60000.0;

const vec3 LIMB_DARKENING = vec3(0.397, 0.503, 0.652);

const float NIGHT_SKY_SUN_HEIGHT = 0.1;

vec3 sun_disk(atmosphere_params params, vec3 origin, vec3 direction)
{
    float angle = asin(clamp(length(cross(direction, params.sun_direction)), 0.0, 1.0));

    if (dot(direction, params.sun_direction) <= 0.0 || angle > params.sun_angular_radius) {
        return vec3(0.0);
    }

    float radius = length(origin);

    vec3 sun_transmittance = sample_transmittance(params, transmittance_lut, radius, dot(direction, origin / radius));

    float edge = angle / params.sun_angular_radius;
    float mu   = sqrt(max(1.0 - edge * edge, 0.0));

    vec3 limb = pow(vec3(mu), LIMB_DARKENING) * (LIMB_DARKENING + 2.0) * 0.5;

    float solid_angle = ATMOSPHERE_PI * params.sun_angular_radius * params.sun_angular_radius;

    return sun_transmittance * params.sun_tint * limb / solid_angle * params.sun_illuminance;
}

void main()
{
    atmosphere_params params = pc.frame.atmosphere.params;

    vec3 direction = normalize(view_ray);
    vec3 origin    = params.camera_position;

    vec3 luminance = sky_luminance(params, sky_view_lut, origin, direction, true);

    bool above_horizon = ray_sphere_nearest(origin, direction, params.bottom_radius) < 0.0;

    if (above_horizon) {
        luminance += sun_disk(params, origin, direction);

        if (params.sun_direction.y < NIGHT_SKY_SUN_HEIGHT) {
            vec3 celestial = normalize(mat3(params.celestial_rotation) * direction);

            float pixel_radius = length(fwidth(direction)) * 0.5;

            float radius = length(origin);

            vec3 transmittance = sample_transmittance(params, transmittance_lut, radius, dot(direction, origin / radius));

            luminance += night_sky_glow(params, transmittance_lut, origin, direction);
            luminance += stars(params, direction, celestial, pixel_radius, pc.frame.exposure) * transmittance;
        }

        luminance += moon_disk(params, transmittance_lut, origin, direction, pc.frame.exposure);
    }

    if (params.clouds_enabled != 0u) {
        vec4 cloud = textureLod(clouds, screen_uv, 0.0);

        luminance = luminance * cloud.a + cloud.rgb / params.cloud_luminance_scale;
    }

    luminance *= params.night_ambient_scale;

    vec3 fog_direction = normalize(vec3(direction.x, max(direction.y, 0.0), direction.z));
    vec3 fog_ambient   = pc.frame.environment_sh.coefficients[0].rgb * FOG_SH_BAND0 * pc.frame.environment_intensity;

    vec4 fog = height_fog(params, params.camera_world_position, params.camera_world_position + fog_direction * params.fog_max_distance, fog_ambient);

    luminance = luminance * fog.a + fog.rgb;

    out_color = vec4(min(luminance * pc.frame.exposure, vec3(MAX_HDR_VALUE)), 1.0);
}
