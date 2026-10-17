#version 450

layout(set = 0, binding = 0) uniform sampler2D hdr_image;

layout(push_constant) uniform constants
{
    uint tonemapper;
} pc;

layout(location = 0) in vec2 frag_uv;

layout(location = 0) out vec4 out_color;

const uint TONEMAPPER_PBR_NEUTRAL = 0u;
const uint TONEMAPPER_ACES        = 1u;

const float NEUTRAL_START_COMPRESSION = 0.8 - 0.04;
const float NEUTRAL_DESATURATION      = 0.15;

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

void main()
{
    vec3 hdr = max(texture(hdr_image, frag_uv).rgb, vec3(0.0));

    vec3 mapped = pc.tonemapper == TONEMAPPER_ACES ? aces_tonemap(hdr) : pbr_neutral_tonemap(hdr);

    out_color = vec4(clamp(mapped, 0.0, 1.0), 1.0);
}
