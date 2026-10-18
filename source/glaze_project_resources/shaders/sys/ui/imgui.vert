#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;

layout(push_constant) uniform constants
{
    vec2 scale;
    vec2 translate;
    uint texture_index;
    uint sampler_index;
} pc;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;

vec3 srgb_to_linear(vec3 color)
{
    return mix(color / 12.92, pow((color + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), color));
}

void main()
{
    out_uv    = in_uv;
    out_color = vec4(srgb_to_linear(in_color.rgb), in_color.a);

    gl_Position = vec4(in_position * pc.scale + pc.translate, 0.0, 1.0);
}
