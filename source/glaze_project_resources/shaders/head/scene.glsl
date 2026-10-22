#extension GL_EXT_nonuniform_qualifier : require

#include "head/scene_types.glsl"

layout(push_constant) uniform constants
{
    frame_buffer frame;
} pc;

layout(set = 0, binding = 0) uniform sampler scene_samplers[64];
layout(set = 0, binding = 1) uniform texture2D scene_textures[4096];

layout(set = 0, binding = 3) uniform sampler2D scene_shadow_pool;
layout(set = 0, binding = 4) uniform sampler2DArray scene_cloud_shadow;
layout(set = 0, binding = 5) uniform sampler2D scene_shadow_mask;
layout(set = 0, binding = 6) uniform sampler3D scene_volumetric_fog;

vec4 sample_scene_texture(uint texture_index, uint sampler_index, vec2 uv)
{
    return texture(
        sampler2D(scene_textures[nonuniformEXT(texture_index)], scene_samplers[nonuniformEXT(sampler_index)]),
        uv
    );
}
