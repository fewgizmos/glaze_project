#version 450

layout(set = 0, binding = 0) uniform sampler2D hdr_image;
layout(set = 0, binding = 1) uniform sampler2D bloom_image;

layout(push_constant) uniform constants
{
    uint tonemapper;
    float sharpen;
    float bloom_intensity;
    float bloom_normalization;
    float inverse_exposure;
    float night_vision;
    float contrast;
    float saturation;
} pc;

layout(location = 0) out vec4 out_color;

const uint TONEMAPPER_PBR_NEUTRAL = 0u;
const uint TONEMAPPER_ACES        = 1u;
const uint TONEMAPPER_AGX         = 2u;

const mat3 AGX_INSET = mat3(
    0.842479062253094,  0.0423282422610123, 0.0423756549057051,
    0.0784335999999992, 0.878468636469772,  0.0784336,
    0.0792237451477643, 0.0791661274605434, 0.879142973793104
);

const mat3 AGX_OUTSET = mat3(
     1.19687900512017,  -0.0528968517574562, -0.0529716355144438,
    -0.0980208811401368, 1.15190312990417,   -0.0980434501171241,
    -0.0990297440797205, -0.0989611768448433, 1.15107367264116
);

const float AGX_MIN_EV      = -12.47393;
const float AGX_MAX_EV      = 4.026069;
const float AGX_MIN_VALUE   = 0.0000000001;
const float AGX_DISPLAY_GAMMA = 2.2;

const vec3 LUMINANCE_WEIGHTS = vec3(0.2126, 0.7152, 0.0722);

const float NEUTRAL_START_COMPRESSION = 0.8 - 0.04;
const float NEUTRAL_DESATURATION      = 0.15;

const float CAS_PEAK_SOFT  = 8.0;
const float CAS_PEAK_SHARP = 5.0;
const float CAS_EPSILON    = 0.0001;

const float DITHER_LEVELS = 255.0;

const float NIGHT_EPSILON          = 0.000000001;
const float SCOTOPIC_LOG_LUMINANCE = -4.0;
const float MESOPIC_LOG_LUMINANCE  = -3.0;
const float PHOTOPIC_LOG_LUMINANCE = -1.0;

const float BLUE_SHIFT              = 0.25;
const float SCOTOPIC_TINT_STRENGTH = 0.4;

const vec3 SCOTOPIC_TINT = vec3(0.85, 0.925, 1.2) / dot(vec3(0.85, 0.925, 1.2), vec3(0.2126, 0.7152, 0.0722));

vec3 aces_tonemap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 agx_contrast_curve(vec3 x)
{
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;

    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 agx_tonemap(vec3 color)
{
    vec3 encoded = clamp(log2(max(AGX_INSET * color, vec3(AGX_MIN_VALUE))), AGX_MIN_EV, AGX_MAX_EV);

    encoded = agx_contrast_curve((encoded - AGX_MIN_EV) / (AGX_MAX_EV - AGX_MIN_EV));

    encoded = pow(max(encoded, vec3(0.0)), vec3(pc.contrast));

    float luminance = dot(encoded, LUMINANCE_WEIGHTS);

    encoded = luminance + pc.saturation * (encoded - luminance);

    return pow(max(AGX_OUTSET * encoded, vec3(0.0)), vec3(AGX_DISPLAY_GAMMA));
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

vec3 night_vision(vec3 color)
{
    if (pc.night_vision <= 0.0) {
        return color;
    }

    vec3 luminance_color = color * pc.inverse_exposure;

    float x = dot(luminance_color, vec3(0.4124, 0.3576, 0.1805));
    float y = dot(luminance_color, vec3(0.2126, 0.7152, 0.0722));
    float z = dot(luminance_color, vec3(0.0193, 0.1192, 0.9505));

    float scotopic = max(y * (1.33 * (1.0 + (y + z) / max(x, NIGHT_EPSILON)) - 1.68), 0.0);

    float log_luminance = log(max(y, NIGHT_EPSILON)) / log(10.0);

    float desaturation = (1.0 - smoothstep(SCOTOPIC_LOG_LUMINANCE, MESOPIC_LOG_LUMINANCE, log_luminance)) * pc.night_vision;
    float blue_shift   = (1.0 - smoothstep(MESOPIC_LOG_LUMINANCE, PHOTOPIC_LOG_LUMINANCE, log_luminance)) * BLUE_SHIFT * pc.night_vision;

    vec3 result = mix(luminance_color, vec3(scotopic), desaturation);

    result *= mix(vec3(1.0), SCOTOPIC_TINT, max(blue_shift, desaturation * SCOTOPIC_TINT_STRENGTH));

    return result / pc.inverse_exposure;
}

vec3 mapped_at(ivec2 coord)
{
    ivec2 size = textureSize(hdr_image, 0);

    ivec2 texel = clamp(coord, ivec2(0), size - 1);

    vec3 hdr   = max(texelFetch(hdr_image, texel, 0).rgb, vec3(0.0));
    vec3 bloom = textureLod(bloom_image, (vec2(texel) + 0.5) / vec2(size), 0.0).rgb * pc.bloom_normalization;

    hdr = mix(hdr, bloom, pc.bloom_intensity);
    hdr = night_vision(hdr);

    vec3 mapped = pc.tonemapper == TONEMAPPER_AGX ? agx_tonemap(hdr) : (pc.tonemapper == TONEMAPPER_ACES ? aces_tonemap(hdr) : pbr_neutral_tonemap(hdr));

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

vec3 linear_to_srgb(vec3 color)
{
    return mix(color * 12.92, 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), color));
}

vec3 srgb_to_linear(vec3 color)
{
    return mix(color / 12.92, pow((color + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), color));
}

float dither_noise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

void main()
{
    vec3 color = linear_to_srgb(contrast_adaptive_sharpen(ivec2(gl_FragCoord.xy)));

    color += (dither_noise(gl_FragCoord.xy) - 0.5) / DITHER_LEVELS;

    out_color = vec4(srgb_to_linear(clamp(color, 0.0, 1.0)), 1.0);
}
