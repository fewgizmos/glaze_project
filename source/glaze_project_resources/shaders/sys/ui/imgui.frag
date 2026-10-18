#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform sampler scene_samplers[64];
layout(set = 0, binding = 1) uniform texture2D scene_textures[4096];

layout(push_constant) uniform constants
{
    vec2 scale;
    vec2 translate;
    uint texture_index;
    uint sampler_index;
} pc;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 out_color;

void main()
{
    vec4 texel = texture(
        sampler2D(scene_textures[nonuniformEXT(pc.texture_index)], scene_samplers[nonuniformEXT(pc.sampler_index)]),
        in_uv
    );

    out_color = in_color * texel;
}
