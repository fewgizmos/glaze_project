#ifndef ATMOSPHERE_GLSL
#define ATMOSPHERE_GLSL

#include "sys/util/atmosphere_params.glsl"

const float ATMOSPHERE_PI = 3.14159265359;

const float ATMOSPHERE_MIN_EXTINCTION = 0.000001;

struct medium_sample
{
    vec3 scattering;
    vec3 extinction;
    vec3 rayleigh_scattering;
    vec3 mie_scattering;
};

float ray_sphere_nearest(vec3 origin, vec3 direction, float radius)
{
    float b = dot(origin, direction);
    float c = dot(origin, origin) - radius * radius;

    float discriminant = b * b - c;

    if (discriminant < 0.0) {
        return -1.0;
    }

    float root = sqrt(discriminant);

    float near_t = -b - root;
    float far_t  = -b + root;

    if (far_t < 0.0) {
        return -1.0;
    }

    return near_t >= 0.0 ? near_t : far_t;
}

medium_sample sample_medium(atmosphere_params params, vec3 position)
{
    float height = length(position) - params.bottom_radius;

    float rayleigh_density   = exp(params.rayleigh_exp_scale * height);
    float mie_density        = exp(params.mie_exp_scale * height);
    float absorption_density = max(0.0, 1.0 - abs(height - params.absorption_center) / params.absorption_width);

    medium_sample result;

    result.rayleigh_scattering = params.rayleigh_scattering * rayleigh_density;
    result.mie_scattering      = params.mie_scattering * mie_density;

    result.scattering = result.rayleigh_scattering + result.mie_scattering;
    result.extinction = result.rayleigh_scattering + params.mie_extinction * mie_density + params.absorption_extinction * absorption_density;

    return result;
}

float rayleigh_phase(float cos_theta)
{
    return 3.0 / (16.0 * ATMOSPHERE_PI) * (1.0 + cos_theta * cos_theta);
}

float mie_phase(float g, float cos_theta)
{
    float g2 = g * g;
    float k  = 3.0 / (8.0 * ATMOSPHERE_PI) * (1.0 - g2) / (2.0 + g2);

    return k * (1.0 + cos_theta * cos_theta) / pow(max(1.0 + g2 - 2.0 * g * cos_theta, 0.000001), 1.5);
}

vec2 transmittance_uv(atmosphere_params params, float radius, float mu)
{
    float horizon = sqrt(max(params.top_radius * params.top_radius - params.bottom_radius * params.bottom_radius, 0.0));
    float rho     = sqrt(max(radius * radius - params.bottom_radius * params.bottom_radius, 0.0));

    float discriminant = radius * radius * (mu * mu - 1.0) + params.top_radius * params.top_radius;
    float distance     = max(0.0, -radius * mu + sqrt(max(discriminant, 0.0)));

    float distance_min = params.top_radius - radius;
    float distance_max = rho + horizon;

    return vec2((distance - distance_min) / max(distance_max - distance_min, 0.000001), rho / horizon);
}

void transmittance_params(atmosphere_params params, vec2 uv, out float radius, out float mu)
{
    float horizon = sqrt(max(params.top_radius * params.top_radius - params.bottom_radius * params.bottom_radius, 0.0));
    float rho     = horizon * uv.y;

    radius = sqrt(rho * rho + params.bottom_radius * params.bottom_radius);

    float distance_min = params.top_radius - radius;
    float distance_max = rho + horizon;
    float distance     = distance_min + uv.x * (distance_max - distance_min);

    mu = distance == 0.0 ? 1.0 : (horizon * horizon - rho * rho - distance * distance) / (2.0 * radius * distance);
    mu = clamp(mu, -1.0, 1.0);
}

vec3 sample_transmittance(atmosphere_params params, sampler2D transmittance_lut, float radius, float mu)
{
    return textureLod(transmittance_lut, transmittance_uv(params, radius, mu), 0.0).rgb;
}

vec3 sample_multi_scattering(atmosphere_params params, sampler2D multi_scattering_lut, float radius, float mu_sun)
{
    vec2 uv = vec2(mu_sun * 0.5 + 0.5, (radius - params.bottom_radius) / (params.top_radius - params.bottom_radius));

    return textureLod(multi_scattering_lut, clamp(uv, 0.0, 1.0), 0.0).rgb;
}

vec3 integrate_luminance(
    atmosphere_params params,
    sampler2D transmittance_lut,
    sampler2D multi_scattering_lut,
    vec3 origin,
    vec3 direction,
    vec3 sun_direction,
    float max_distance,
    uint sample_count,
    bool use_multi_scattering,
    bool include_ground,
    out vec3 transmittance
)
{
    transmittance = vec3(1.0);

    float bottom_t = ray_sphere_nearest(origin, direction, params.bottom_radius);
    float top_t    = ray_sphere_nearest(origin, direction, params.top_radius);

    if (bottom_t < 0.0 && top_t < 0.0) {
        return vec3(0.0);
    }

    float end_t = bottom_t >= 0.0 ? bottom_t : top_t;

    bool hits_ground = bottom_t >= 0.0 && max_distance >= bottom_t;

    end_t = min(end_t, max_distance);

    float cos_theta = dot(sun_direction, direction);

    float phase_rayleigh = rayleigh_phase(cos_theta);
    float phase_mie      = mie_phase(params.mie_phase_g, cos_theta);

    vec3 luminance = vec3(0.0);

    for (uint i = 0u; i < sample_count; i++) {
        float t0 = end_t * float(i) / float(sample_count);
        float t1 = end_t * float(i + 1u) / float(sample_count);
        float dt = t1 - t0;

        vec3 position = origin + direction * (0.5 * (t0 + t1));

        medium_sample medium = sample_medium(params, position);

        vec3 step_transmittance = exp(-medium.extinction * dt);

        float radius = length(position);
        float mu_sun = dot(sun_direction, position / radius);

        vec3 sun_transmittance = sample_transmittance(params, transmittance_lut, radius, mu_sun);

        float planet_shadow = ray_sphere_nearest(position, sun_direction, params.bottom_radius) >= 0.0 ? 0.0 : 1.0;

        vec3 phase_scattering = medium.rayleigh_scattering * phase_rayleigh + medium.mie_scattering * phase_mie;

        vec3 multi_scattering = use_multi_scattering
            ? sample_multi_scattering(params, multi_scattering_lut, radius, mu_sun) * medium.scattering
            : vec3(0.0);

        vec3 scattered = planet_shadow * sun_transmittance * phase_scattering + multi_scattering;

        vec3 integrated = (scattered - scattered * step_transmittance) / max(medium.extinction, vec3(ATMOSPHERE_MIN_EXTINCTION));

        luminance     += transmittance * integrated;
        transmittance *= step_transmittance;
    }

    if (include_ground && hits_ground) {
        vec3 position = origin + direction * bottom_t;

        float radius = length(position);
        vec3  up     = position / radius;

        float mu_sun = dot(sun_direction, up);

        vec3 sun_transmittance = sample_transmittance(params, transmittance_lut, radius, mu_sun);

        luminance += transmittance * sun_transmittance * max(mu_sun, 0.0) * params.ground_albedo / ATMOSPHERE_PI;
    }

    return luminance;
}

vec2 sky_view_uv(atmosphere_params params, bool intersects_ground, float view_zenith_cos, float light_view_cos, float radius)
{
    float horizon_distance = sqrt(max(radius * radius - params.bottom_radius * params.bottom_radius, 0.0));

    float beta                 = acos(clamp(horizon_distance / radius, -1.0, 1.0));
    float zenith_horizon_angle = ATMOSPHERE_PI - beta;

    float view_zenith = acos(clamp(view_zenith_cos, -1.0, 1.0));

    vec2 uv;

    if (!intersects_ground) {
        float coord = view_zenith / zenith_horizon_angle;

        uv.y = (1.0 - sqrt(max(1.0 - coord, 0.0))) * 0.5;
    } else {
        float coord = (view_zenith - zenith_horizon_angle) / beta;

        uv.y = sqrt(max(coord, 0.0)) * 0.5 + 0.5;
    }

    uv.x = sqrt(clamp(-light_view_cos * 0.5 + 0.5, 0.0, 1.0));

    return uv;
}

void sky_view_params(atmosphere_params params, vec2 uv, float radius, out float view_zenith_cos, out float light_view_cos)
{
    float horizon_distance = sqrt(max(radius * radius - params.bottom_radius * params.bottom_radius, 0.0));

    float beta                 = acos(clamp(horizon_distance / radius, -1.0, 1.0));
    float zenith_horizon_angle = ATMOSPHERE_PI - beta;

    if (uv.y < 0.5) {
        float coord = 1.0 - 2.0 * uv.y;

        coord = 1.0 - coord * coord;

        view_zenith_cos = cos(zenith_horizon_angle * coord);
    } else {
        float coord = uv.y * 2.0 - 1.0;

        view_zenith_cos = cos(zenith_horizon_angle + beta * coord * coord);
    }

    float coord = uv.x * uv.x;

    light_view_cos = -(coord * 2.0 - 1.0);
}

const uint SKY_VIEW_SUN_LAYER  = 0u;
const uint SKY_VIEW_MOON_LAYER = 1u;

const float NIGHT_SKY_LAYER_HEIGHT = 100.0;

const float HORIZON_AZIMUTH_FADE = 0.15;

const vec3 NIGHT_SKY_COLOR = vec3(0.9, 1.0, 0.85);

vec3 moon_illuminance_color(atmosphere_params params)
{
    return params.moon_color * params.moon_illuminance;
}

vec3 sample_sky_view(
    atmosphere_params params,
    sampler2DArray sky_view_lut,
    vec3 origin,
    vec3 direction,
    bool clamp_to_horizon,
    uint layer,
    vec3 light_direction
)
{
    float radius = length(origin);

    vec2 light_horizontal = light_direction.xz;
    vec2 view_horizontal  = direction.xz;

    float light_view_cos = 1.0;

    if (dot(light_horizontal, light_horizontal) > 0.000001 && dot(view_horizontal, view_horizontal) > 0.000001) {
        light_view_cos = dot(normalize(light_horizontal), normalize(view_horizontal));
    }

    bool intersects_ground = ray_sphere_nearest(origin, direction, params.bottom_radius) >= 0.0;

    if (clamp_to_horizon && intersects_ground) {
        light_view_cos = mix(light_view_cos, 0.0, smoothstep(0.0, HORIZON_AZIMUTH_FADE, -direction.y));
    }

    vec2 uv = sky_view_uv(params, intersects_ground, direction.y, light_view_cos, radius);

    if (clamp_to_horizon) {
        uv.y = min(uv.y, 0.5 - 0.5 / float(textureSize(sky_view_lut, 0).y));
    }

    return textureLod(sky_view_lut, vec3(uv, float(layer)), 0.0).rgb;
}

vec3 sky_luminance(atmosphere_params params, sampler2DArray sky_view_lut, vec3 origin, vec3 direction, bool clamp_to_horizon)
{
    vec3 sun  = sample_sky_view(params, sky_view_lut, origin, direction, clamp_to_horizon, SKY_VIEW_SUN_LAYER, params.sun_direction);
    vec3 moon = sample_sky_view(params, sky_view_lut, origin, direction, clamp_to_horizon, SKY_VIEW_MOON_LAYER, params.moon_direction);

    vec3 luminance = sun * params.sun_illuminance + moon * moon_illuminance_color(params);

    float bluish = clamp((luminance.b - luminance.r) / max(luminance.b, 0.000001), 0.0, 1.0);

    return mix(luminance, vec3(dot(luminance, vec3(0.2126, 0.7152, 0.0722))), params.twilight_desaturation * bluish);
}

vec3 night_sky_glow(atmosphere_params params, sampler2D transmittance_lut, vec3 origin, vec3 direction)
{
    float radius = length(origin);
    float mu     = max(direction.y, 0.0);

    float ratio = params.bottom_radius / (params.bottom_radius + NIGHT_SKY_LAYER_HEIGHT);

    float van_rhijn = 1.0 / sqrt(max(1.0 - ratio * ratio * (1.0 - mu * mu), 0.000001));

    vec3 transmittance = sample_transmittance(params, transmittance_lut, radius, mu);

    return NIGHT_SKY_COLOR * params.night_sky_luminance * van_rhijn * transmittance;
}

#endif
