#version 450
#extension GL_EXT_buffer_reference : require

#include "util/scene.glsl"

layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec2 frag_uv;
layout(location = 4) in vec4 frag_tangent;

layout(location = 5) flat in uint frag_material_index;

layout(location = 6) in vec4 frag_current_clip;
layout(location = 7) in vec4 frag_previous_clip;

layout(location = 0) out vec4 out_normal;
layout(location = 1) out vec2 out_velocity;

const float MIN_TANGENT_LENGTH2 = 0.000001;

vec3 surface_normal(material_data material)
{
    vec3 normal = normalize(frag_normal);

    vec3 orthogonal_tangent = frag_tangent.xyz - normal * dot(normal, frag_tangent.xyz);

    if (material.normal_texture != NO_TEXTURE && frag_tangent.w != 0.0 && dot(orthogonal_tangent, orthogonal_tangent) > MIN_TANGENT_LENGTH2) {
        vec3 tangent   = normalize(orthogonal_tangent);
        vec3 bitangent = cross(normal, tangent) * frag_tangent.w;

        vec3 sampled = sample_scene_texture(material.normal_texture, material.normal_sampler, material_uv(material, TEXTURE_SLOT_NORMAL, frag_uv)).xyz * 2.0 - 1.0;

        sampled.xy *= material.normal_scale;

        normal = normalize(mat3(tangent, bitangent, normal) * sampled);
    }

    return gl_FrontFacing ? normal : -normal;
}

void main()
{
    material_data material = pc.frame.materials.data[frag_material_index];

    float alpha = material.base_color.a;

    if (material.base_color_texture != NO_TEXTURE) {
        alpha *= sample_scene_texture(material.base_color_texture, material.base_color_sampler, material_uv(material, TEXTURE_SLOT_BASE_COLOR, frag_uv)).a;
    }

    if (material.alpha_mode == ALPHA_MODE_MASK && alpha < material.alpha_cutoff) {
        discard;
    }

    vec3 view_normal = normalize(mat3(pc.frame.view) * surface_normal(material));

    vec2 current_ndc  = frag_current_clip.xy / frag_current_clip.w;
    vec2 previous_ndc = frag_previous_clip.xy / frag_previous_clip.w;

    out_normal   = vec4(view_normal, 1.0);
    out_velocity = (current_ndc - previous_ndc) * 0.5;
}
