layout(set = 0, binding = 0) uniform sampler scene_samplers[64];
layout(set = 0, binding = 1) uniform texture2D scene_textures[4096];

vec4 sample_scene_texture(uint texture_index, uint sampler_index, vec2 uv)
{
    return texture(
        sampler2D(scene_textures[nonuniformEXT(texture_index)], scene_samplers[nonuniformEXT(sampler_index)]),
        uv
    );
}
