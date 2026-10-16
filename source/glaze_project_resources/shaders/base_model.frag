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

layout(set = 1, binding = 1) uniform samplerCube env_irradiance;
layout(set = 1, binding = 2) uniform samplerCube env_prefiltered;
layout(set = 1, binding = 3) uniform sampler2D env_brdf_lut;

const vec2  CHECKER_CELLS = vec2(16.0, 8.0);
const float CHECKER_DARK  = 0.35;

const float CONE_EDGE_EPSILON = 0.0001;

const float PI = 3.14159265359;

const vec3  DIELECTRIC_F0 = vec3(0.04);
const float MIN_ROUGHNESS = 0.045;
const float MIN_N_DOT_V   = 0.0001;

const float MIN_TANGENT_LENGTH2 = 0.000001;

const float PREFILTERED_MAX_LOD = 4.0;

const float SPECULAR_AA_VARIANCE  = 0.25;
const float SPECULAR_AA_THRESHOLD = 0.18;

const float HORIZON_FADE = 1.3;

const float SUN_ANGULAR_RADIUS = 0.00465;
const float SUN_COS_RADIUS     = cos(SUN_ANGULAR_RADIUS);
const float SUN_SIN_RADIUS     = sin(SUN_ANGULAR_RADIUS);

struct surface_data
{
    vec3 albedo;
    vec3 normal;
    vec3 geometric_normal;
    vec3 view;
    vec3 reflected;
    vec3 f0;

    vec2 dfg;

    float metallic;
    float roughness;
    float alpha;
    float n_dot_v;
    float occlusion;
    float specular_occlusion;
    float energy_compensation;
};

float checker_at(vec2 uv)
{
    vec2 cell = floor(uv * CHECKER_CELLS);

    return mod(cell.x + cell.y, 2.0) < 1.0 ? 1.0 : CHECKER_DARK;
}

float distribution_ggx(float n_dot_h, float alpha)
{
    float alpha2 = alpha * alpha;
    float factor = n_dot_h * n_dot_h * (alpha2 - 1.0) + 1.0;

    return alpha2 / (PI * factor * factor);
}

float visibility_smith_ggx_correlated(float n_dot_v, float n_dot_l, float alpha)
{
    float alpha2 = alpha * alpha;

    float ggx_v = n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0 - alpha2) + alpha2);
    float ggx_l = n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0 - alpha2) + alpha2);

    return 0.5 / max(ggx_v + ggx_l, 0.000001);
}

vec3 fresnel_schlick(float cos_theta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

vec3 fresnel_schlick_roughness(float cos_theta, vec3 f0, float roughness)
{
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

float specular_anti_aliasing(vec3 normal, float roughness)
{
    vec3 du = dFdx(normal);
    vec3 dv = dFdy(normal);

    float variance = SPECULAR_AA_VARIANCE * (dot(du, du) + dot(dv, dv));
    float kernel   = min(2.0 * variance, SPECULAR_AA_THRESHOLD);

    float alpha  = roughness * roughness;
    float alpha2 = clamp(alpha * alpha + kernel, 0.0, 1.0);

    return sqrt(sqrt(alpha2));
}

float specular_occlusion(float n_dot_v, float occlusion, float roughness)
{
    return clamp(pow(n_dot_v + occlusion, exp2(-16.0 * roughness - 1.0)) - 1.0 + occlusion, 0.0, 1.0);
}

float horizon_occlusion(vec3 reflected, vec3 geometric_normal)
{
    float horizon = clamp(1.0 + HORIZON_FADE * dot(reflected, geometric_normal), 0.0, 1.0);

    return horizon * horizon;
}

vec3 sun_specular_direction(surface_data surface, vec3 to_light)
{
    float d = dot(to_light, surface.reflected);

    if (d >= SUN_COS_RADIUS) {
        return surface.reflected;
    }

    vec3 tangent = surface.reflected - d * to_light;

    if (dot(tangent, tangent) <= MIN_TANGENT_LENGTH2) {
        return to_light;
    }

    return normalize(to_light * SUN_COS_RADIUS + normalize(tangent) * SUN_SIN_RADIUS);
}

vec3 brdf(surface_data surface, vec3 to_light, vec3 specular_to_light)
{
    float n_dot_l = clamp(dot(surface.normal, to_light), 0.0, 1.0);

    if (n_dot_l <= 0.0) {
        return vec3(0.0);
    }

    float specular_n_dot_l = clamp(dot(surface.normal, specular_to_light), 0.0, 1.0);

    vec3 half_vector = normalize(surface.view + specular_to_light);

    float n_dot_h = clamp(dot(surface.normal, half_vector), 0.0, 1.0);
    float v_dot_h = clamp(dot(surface.view, half_vector), 0.0, 1.0);

    vec3 fresnel = fresnel_schlick(v_dot_h, surface.f0);

    vec3 specular = distribution_ggx(n_dot_h, surface.alpha)
                  * visibility_smith_ggx_correlated(surface.n_dot_v, max(specular_n_dot_l, MIN_N_DOT_V), surface.alpha)
                  * fresnel
                  * surface.energy_compensation
                  * specular_n_dot_l;

    vec3 diffuse = (1.0 - fresnel) * (1.0 - surface.metallic) * surface.albedo / PI * n_dot_l;

    return diffuse + specular;
}

vec3 surface_normal(material_data material)
{
    vec3 normal = normalize(frag_normal);

    vec3 orthogonal_tangent = frag_tangent.xyz - normal * dot(normal, frag_tangent.xyz);

    if (material.normal_texture != NO_TEXTURE && frag_tangent.w != 0.0 && dot(orthogonal_tangent, orthogonal_tangent) > MIN_TANGENT_LENGTH2) {
        vec3 tangent   = normalize(orthogonal_tangent);
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

vec3 ambient_contribution(light_data light, surface_data surface)
{
    vec3 irradiance = mix(light.ground_color, light.color, surface.normal.y * 0.5 + 0.5);
    vec3 reflection = mix(light.ground_color, light.color, surface.reflected.y * 0.5 + 0.5);

    vec3 fresnel = fresnel_schlick_roughness(surface.n_dot_v, surface.f0, surface.roughness);

    vec3 diffuse  = (1.0 - fresnel) * (1.0 - surface.metallic) * surface.albedo * irradiance * surface.occlusion;
    vec3 specular = fresnel * reflection * surface.specular_occlusion;

    return diffuse + specular;
}

vec3 environment_contribution(surface_data surface)
{
    vec3 irradiance = texture(env_irradiance, surface.normal).rgb;
    vec3 radiance   = textureLod(env_prefiltered, surface.reflected, surface.roughness * PREFILTERED_MAX_LOD).rgb;

    vec3 fresnel = fresnel_schlick_roughness(surface.n_dot_v, surface.f0, surface.roughness);

    vec3 single_scatter = fresnel * surface.dfg.x + surface.dfg.y;

    float single_energy = surface.dfg.x + surface.dfg.y;
    float multi_energy  = 1.0 - single_energy;

    vec3 average_fresnel = surface.f0 + (1.0 - surface.f0) / 21.0;
    vec3 multi_scatter   = single_scatter * average_fresnel / (1.0 - multi_energy * average_fresnel);

    vec3 diffuse_color = surface.albedo * (1.0 - surface.metallic);
    vec3 diffuse       = diffuse_color * (1.0 - single_scatter - multi_scatter * multi_energy);

    vec3 specular = single_scatter * radiance * surface.specular_occlusion;
    vec3 ambient  = (multi_scatter * multi_energy + diffuse) * irradiance * surface.occlusion;

    return (specular + ambient) * pc.frame.environment_intensity;
}

vec3 light_contribution(light_data light, surface_data surface)
{
    if (light.type == LIGHT_TYPE_DIRECTIONAL) {
        vec3 to_light = -light.direction;

        return light.color * brdf(surface, to_light, sun_specular_direction(surface, to_light));
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

        return light.color * brdf(surface, to_light, to_light) * attenuation * cone_factor(light, to_light);
    }

    if (light.type == LIGHT_TYPE_AMBIENT && pc.frame.has_environment == 0u) {
        return ambient_contribution(light, surface);
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

    surface_data data;

    data.albedo           = material.base_color.rgb * frag_color * surface;
    data.normal           = surface_normal(material);
    data.geometric_normal = gl_FrontFacing ? normalize(frag_normal) : -normalize(frag_normal);
    data.view             = normalize(pc.frame.camera_pos - frag_world_pos);
    data.reflected        = reflect(-data.view, data.normal);

    data.metallic  = clamp(metallic, 0.0, 1.0);
    data.roughness = clamp(specular_anti_aliasing(data.normal, clamp(roughness, 0.0, 1.0)), MIN_ROUGHNESS, 1.0);
    data.alpha     = data.roughness * data.roughness;
    data.n_dot_v   = clamp(dot(data.normal, data.view), MIN_N_DOT_V, 1.0);
    data.occlusion = occlusion;

    data.specular_occlusion = specular_occlusion(data.n_dot_v, occlusion, data.roughness)
                            * horizon_occlusion(data.reflected, data.geometric_normal);

    data.f0  = mix(DIELECTRIC_F0, data.albedo, data.metallic);
    data.dfg = texture(env_brdf_lut, vec2(data.n_dot_v, data.roughness)).rg;

    data.energy_compensation = 1.0 + max(data.f0.r, max(data.f0.g, data.f0.b)) * (1.0 / max(data.dfg.x + data.dfg.y, 0.0001) - 1.0);

    vec3 lighting = vec3(0.0);

    if (pc.frame.has_environment != 0u) {
        lighting += environment_contribution(data);
    }

    for (uint i = 0u; i < pc.frame.light_count; i++) {
        lighting += light_contribution(pc.frame.lights.data[i], data);
    }

    out_color = vec4(lighting + emissive, 1.0);
}
