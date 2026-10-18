#ifndef HEIGHT_FOG_GLSL
#define HEIGHT_FOG_GLSL

#include "sys/util/atmosphere_params.glsl"

const float FOG_PI = 3.14159265359;

const float FOG_SH_BAND0 = 0.282095;

const float FOG_MIN_EXPONENT     = -60.0;
const float FOG_LINEAR_THRESHOLD = 0.0001;

float fog_phase(float g, float cos_theta)
{
    float g2 = g * g;

    return (1.0 - g2) / (4.0 * FOG_PI * pow(max(1.0 + g2 - 2.0 * g * cos_theta, 0.000001), 1.5));
}

float height_fog_optical_depth(atmosphere_params params, vec3 camera, vec3 point)
{
    vec3 ray = point - camera;

    float distance = length(ray);

    float camera_density = params.fog_density * exp(-params.fog_height_falloff * (camera.y - params.fog_base_height));

    float exponent = max(params.fog_height_falloff * ray.y, FOG_MIN_EXPONENT);

    float factor = abs(exponent) > FOG_LINEAR_THRESHOLD ? (1.0 - exp(-exponent)) / exponent : 1.0 - 0.5 * exponent;

    return camera_density * factor * distance;
}

vec4 height_fog(atmosphere_params params, vec3 camera, vec3 point, vec3 ambient)
{
    if (params.fog_density <= 0.0) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }

    float optical_depth = height_fog_optical_depth(params, camera, point);

    float transmittance = exp(-optical_depth);

    vec3 view = normalize(point - camera);

    vec3 light = ambient
        + params.fog_sun_illuminance * fog_phase(params.fog_anisotropy, dot(view, params.sun_direction))
        + params.fog_moon_illuminance * fog_phase(params.fog_anisotropy, dot(view, params.moon_direction));

    return vec4(light * params.fog_color * (1.0 - transmittance), transmittance);
}

#endif
