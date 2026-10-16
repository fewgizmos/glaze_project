#version 450
#extension GL_EXT_buffer_reference : require

#include "util/scene.glsl"

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec2 frag_uv;
layout(location = 3) in vec3 frag_world_pos;
layout(location = 4) in vec4 frag_tangent;

layout(location = 5) flat in uint frag_material_index;

layout(location = 0) out vec4 out_color;

const vec2  CHECKER_CELLS = vec2(16.0, 8.0);
const float CHECKER_DARK  = 0.35;

const float CONE_EDGE_EPSILON = 0.0001;

float checker_at(vec2 uv)
{
    vec2 cell = floor(uv * CHECKER_CELLS);

    return mod(cell.x + cell.y, 2.0) < 1.0 ? 1.0 : CHECKER_DARK;
}

vec3 aces_tonemap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 surface_normal(material_data material)
{
    vec3 normal = normalize(frag_normal);

    if (material.normal_texture != NO_TEXTURE && frag_tangent.w != 0.0) {
        vec3 tangent   = normalize(frag_tangent.xyz - normal * dot(normal, frag_tangent.xyz));
        vec3 bitangent = cross(normal, tangent) * frag_tangent.w;

        vec3 sampled = sample_scene_texture(material.normal_texture, material.normal_sampler, frag_uv).xyz * 2.0 - 1.0;

        sampled.xy *= material.normal_scale;

        normal = normalize(mat3(tangent, bitangent, normal) * sampled);
    }

    return gl_FrontFacing ? normal : -normal;
}

float cone_factor(light_data light, vec3 to_light)
{
    if (light.cos_outer <= -1.0) {
        return 1.0;
    }

    float cos_angle = dot(-to_light, light.direction);

    return smoothstep(light.cos_outer, max(light.cos_inner, light.cos_outer + CONE_EDGE_EPSILON), cos_angle);
}

vec3 light_contribution(light_data light, vec3 normal, float occlusion)
{
    if (light.type == LIGHT_TYPE_DIRECTIONAL) {
        return light.color * max(dot(normal, -light.direction), 0.0);
    }

    if (light.type == LIGHT_TYPE_POINT) {
        vec3  offset   = light.position - frag_world_pos;
        float distance = length(offset);

        if (distance >= light.range || distance <= 0.0) {
            return vec3(0.0);
        }

        vec3 to_light = offset / distance;

        float window      = clamp(1.0 - pow(distance / light.range, 4.0), 0.0, 1.0);
        float attenuation = window * window / (distance * distance + 1.0);

        return light.color * max(dot(normal, to_light), 0.0) * attenuation * cone_factor(light, to_light);
    }

    if (light.type == LIGHT_TYPE_AMBIENT) {
        return mix(light.ground_color, light.color, normal.y * 0.5 + 0.5) * occlusion;
    }

    return vec3(0.0);
}

void main()
{
    material_data material = pc.frame.materials.data[frag_material_index];

    vec3 surface = vec3(checker_at(frag_uv));
    float alpha  = material.base_color.a;

    if (material.base_color_texture != NO_TEXTURE) {
        vec4 sampled = sample_scene_texture(material.base_color_texture, material.base_color_sampler, frag_uv);

        surface = sampled.rgb;
        alpha  *= sampled.a;
    }

    if (material.alpha_mode == ALPHA_MODE_MASK && alpha < material.alpha_cutoff) {
        discard;
    }

    float metallic  = material.metallic;
    float roughness = material.roughness;

    if (material.metallic_roughness_texture != NO_TEXTURE) {
        vec4 sampled = sample_scene_texture(material.metallic_roughness_texture, material.metallic_roughness_sampler, frag_uv);

        metallic  *= sampled.b;
        roughness *= sampled.g;
    }

    float occlusion = 1.0;

    if (material.occlusion_texture != NO_TEXTURE) {
        float sampled = sample_scene_texture(material.occlusion_texture, material.occlusion_sampler, frag_uv).r;

        occlusion = 1.0 + material.occlusion_strength * (sampled - 1.0);
    }

    vec3 emissive = material.emissive;

    if (material.emissive_texture != NO_TEXTURE) {
        emissive *= sample_scene_texture(material.emissive_texture, material.emissive_sampler, frag_uv).rgb;
    }

    vec3 albedo = material.base_color.rgb * frag_color * surface;
    vec3 normal = surface_normal(material);

    vec3 lighting = vec3(0.0);

    for (uint i = 0u; i < pc.frame.light_count; i++) {
        lighting += light_contribution(pc.frame.lights.data[i], normal, occlusion);
    }

    out_color = vec4(aces_tonemap(albedo * lighting + emissive), 1.0);
}
