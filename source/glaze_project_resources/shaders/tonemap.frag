#version 450

layout(set = 0, binding = 0) uniform sampler2D hdr_image;

layout(push_constant) uniform constants
{
    float exposure;
} pc;

layout(location = 0) in vec2 frag_uv;

layout(location = 0) out vec4 out_color;

vec3 aces_tonemap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdr = texture(hdr_image, frag_uv).rgb * pc.exposure;

    out_color = vec4(aces_tonemap(hdr), 1.0);
}
