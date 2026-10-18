#ifndef NIGHT_SKY_GLSL
#define NIGHT_SKY_GLSL

#include "head/atmosphere.glsl"
#include "head/noise.glsl"

const float STAR_GRID = 64.0;

const float STAR_MAX_MAGNITUDE = 8.0;
const float STAR_MIN_MAGNITUDE = -1.5;

const float STAR_ZERO_MAGNITUDE_ILLUMINANCE = 0.00000254;

const float STAR_MIN_TEMPERATURE = 3000.0;
const float STAR_MAX_TEMPERATURE = 12000.0;

const float TWINKLE_AMOUNT  = 0.1;
const float TWINKLE_ZENITH  = 0.15;
const float TWINKLE_SPEED_A = 2.0;
const float TWINKLE_SPEED_B = 3.0;

const float STAR_DISPLAY_LUMINANCE = 1.5;

const float MOON_DISPLAY_LUMINANCE = 2.0;

const vec3 LUMINANCE_WEIGHTS = vec3(0.2126, 0.7152, 0.0722);

vec3 star_color(float temperature)
{
    float t = clamp((temperature - STAR_MIN_TEMPERATURE) / (STAR_MAX_TEMPERATURE - STAR_MIN_TEMPERATURE), 0.0, 1.0);

    vec3 cool = vec3(1.0, 0.62, 0.35);
    vec3 sun  = vec3(1.0, 0.93, 0.85);
    vec3 hot  = vec3(0.68, 0.78, 1.0);

    vec3 color = t < 0.3 ? mix(cool, sun, t / 0.3) : mix(sun, hot, (t - 0.3) / 0.7);

    return color / dot(color, LUMINANCE_WEIGHTS);
}

vec3 stars(atmosphere_params params, vec3 world_direction, vec3 celestial_direction, float pixel_radius, float exposure)
{
    vec3 cell_position = celestial_direction * STAR_GRID;
    ivec3 base_cell    = ivec3(floor(cell_position));

    float sigma = max(pixel_radius, 0.00001);

    float twinkle_strength = params.star_twinkle * TWINKLE_AMOUNT * (TWINKLE_ZENITH + (1.0 - TWINKLE_ZENITH) * pow(1.0 - clamp(world_direction.y, 0.0, 1.0), 3.0));

    vec3 result = vec3(0.0);

    for (int z = -1; z <= 1; z++) {
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                ivec3 cell = base_cell + ivec3(x, y, z);

                vec3 position = vec3(cell) + hash33(cell);

                if (abs(length(position) - STAR_GRID) > 0.5) {
                    continue;
                }

                vec3 star_direction = normalize(position);

                if (dot(celestial_direction, star_direction) <= 0.0) {
                    continue;
                }

                float angle = length(cross(celestial_direction, star_direction));

                if (angle > sigma * 4.0) {
                    continue;
                }

                vec3 properties = hash33(cell + ivec3(17, 59, 113));

                float magnitude = max(STAR_MAX_MAGNITUDE + 2.0 * log(max(properties.x, 0.000001)) / log(10.0), STAR_MIN_MAGNITUDE);

                float illuminance = STAR_ZERO_MAGNITUDE_ILLUMINANCE * pow(10.0, -0.4 * magnitude);

                float temperature = mix(STAR_MIN_TEMPERATURE, STAR_MAX_TEMPERATURE, properties.y * properties.y);

                float phase   = properties.z * 6.2831853;
                float twinkle = 1.0 + twinkle_strength * sin(params.time * TWINKLE_SPEED_A + phase) * sin(params.time * TWINKLE_SPEED_B + phase * 2.3);

                float peak   = illuminance / (2.0 * ATMOSPHERE_PI * sigma * sigma);
                float spread = exp(-angle * angle / (2.0 * sigma * sigma)) * min(peak, STAR_DISPLAY_LUMINANCE / max(exposure, 0.000001));

                result += star_color(temperature) * spread * max(twinkle, 0.0);
            }
        }
    }

    return result * params.star_intensity;
}

vec3 moon_disk(atmosphere_params params, sampler2D transmittance_lut, vec3 origin, vec3 direction, float exposure)
{
    float radius_angle = params.moon_angular_radius;

    float cos_angle = dot(direction, params.moon_direction);
    float angle     = asin(clamp(length(cross(direction, params.moon_direction)), 0.0, 1.0));

    if (cos_angle <= 0.0 || angle > radius_angle + params.moon_flare_size) {
        return vec3(0.0);
    }

    float origin_radius = length(origin);

    vec3 transmittance = sample_transmittance(params, transmittance_lut, origin_radius, dot(direction, origin / origin_radius));

    vec3 luminance = moon_illuminance_color(params) / (ATMOSPHERE_PI * radius_angle * radius_angle);

    float peak = max(luminance.r, max(luminance.g, luminance.b)) * exposure;

    luminance *= min(1.0, MOON_DISPLAY_LUMINANCE / max(peak, 0.000001)) * transmittance;

    if (angle > radius_angle) {
        float flare = pow(clamp(1.0 - (angle - radius_angle) / max(params.moon_flare_size, 0.000001), 0.0, 1.0), params.moon_flare_falloff);

        return params.moon_flare_tint * params.moon_flare_multiplier * flare * luminance;
    }

    return luminance;
}

#endif
