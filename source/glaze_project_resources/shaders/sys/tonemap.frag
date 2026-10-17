#version 450

layout(set = 0, binding = 0) uniform sampler2D hdr_image;
layout(set = 0, binding = 1) uniform sampler2D bloom_image;

layout(push_constant) uniform constants
{
    uint tonemapper;
    float sharpen;
    float bloom_intensity;
    float bloom_normalization;
} pc;

layout(location = 0) out vec4 out_color;

const uint TONEMAPPER_PBR_NEUTRAL = 0u;
const uint TONEMAPPER_ACES        = 1u;

const float NEUTRAL_START_COMPRESSION = 0.8 - 0.04;
const float NEUTRAL_DESATURATION      = 0.15;

const float CAS_PEAK_SOFT  = 8.0;
const float CAS_PEAK_SHARP = 5.0;
const float CAS_EPSILON    = 0.0001;

vec3 aces_tonemap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 pbr_neutral_tonemap(vec3 color)
{
    float x      = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;

    color -= offset;

    float peak = max(color.r, max(color.g, color.b));

    if (peak < NEUTRAL_START_COMPRESSION) {
        return color;
    }

    float d        = 1.0 - NEUTRAL_START_COMPRESSION;
    float new_peak = 1.0 - d * d / (peak + d - NEUTRAL_START_COMPRESSION);

    color *= new_peak / peak;

    float g = 1.0 - 1.0 / (NEUTRAL_DESATURATION * (peak - new_peak) + 1.0);

    return mix(color, vec3(new_peak), g);
}

vec3 mapped_at(ivec2 coord)
{
    ivec2 size = textureSize(hdr_image, 0);

    ivec2 texel = clamp(coord, ivec2(0), size - 1);

    vec3 hdr   = max(texelFetch(hdr_image, texel, 0).rgb, vec3(0.0));
    vec3 bloom = textureLod(bloom_image, (vec2(texel) + 0.5) / vec2(size), 0.0).rgb * pc.bloom_normalization;

    hdr = mix(hdr, bloom, pc.bloom_intensity);

    vec3 mapped = pc.tonemapper == TONEMAPPER_ACES ? aces_tonemap(hdr) : pbr_neutral_tonemap(hdr);

    return clamp(mapped, 0.0, 1.0);
}

vec3 contrast_adaptive_sharpen(ivec2 coord)
{
    vec3 up     = mapped_at(coord + ivec2( 0, -1));
    vec3 left   = mapped_at(coord + ivec2(-1,  0));
    vec3 center = mapped_at(coord);
    vec3 right  = mapped_at(coord + ivec2( 1,  0));
    vec3 down   = mapped_at(coord + ivec2( 0,  1));

    if (pc.sharpen <= 0.0) {
        return center;
    }

    vec3 minimum = min(min(min(up, left), min(center, right)), down);
    vec3 maximum = max(max(max(up, left), max(center, right)), down);

    vec3 amplitude = sqrt(clamp(min(minimum, 2.0 - maximum) / max(maximum, vec3(CAS_EPSILON)), 0.0, 1.0));

    vec3 weight = amplitude * (-1.0 / mix(CAS_PEAK_SOFT, CAS_PEAK_SHARP, clamp(pc.sharpen, 0.0, 1.0)));

    return clamp((up * weight + left * weight + right * weight + down * weight + center) / (1.0 + 4.0 * weight), 0.0, 1.0);
}

void main()
{
    out_color = vec4(contrast_adaptive_sharpen(ivec2(gl_FragCoord.xy)), 1.0);
}
