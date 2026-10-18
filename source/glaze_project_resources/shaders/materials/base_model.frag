#version 450
#extension GL_EXT_buffer_reference : require

#include "head/scene.glsl"
#include "head/height_fog.glsl"

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec2 frag_uv;
layout(location = 3) in vec3 frag_world_pos;
layout(location = 4) in vec4 frag_tangent;

layout(location = 5) flat in uint frag_material_index;

layout(location = 0) out vec4 out_color;

layout(set = 1, binding = 1) uniform samplerCube env_prefiltered;
layout(set = 1, binding = 2) uniform sampler2D env_brdf_lut;
layout(set = 1, binding = 3) uniform sampler3D env_aerial_perspective;
layout(set = 1, binding = 4) uniform sampler3D env_moon_aerial_perspective;

layout(set = 0, binding = 2) uniform sampler2D scene_gtao;

const vec2  CHECKER_CELLS = vec2(16.0, 8.0);
const float CHECKER_DARK  = 0.35;

const float CONE_EDGE_EPSILON = 0.0001;

const float PI = 3.14159265359;

const float MIN_ROUGHNESS = 0.045;
const float MIN_N_DOT_V   = 0.0001;

const float MIN_TANGENT_LENGTH2 = 0.000001;

const float PARALLAX_MIN_LAYERS = 8.0;
const float PARALLAX_MAX_LAYERS = 32.0;
const float PARALLAX_MIN_VIEW_Z = 0.1;
const float PARALLAX_EPSILON    = 0.00001;

const float PREFILTERED_MAX_LOD = 4.0;

const float SPECULAR_AA_VARIANCE  = 0.25;
const float SPECULAR_AA_THRESHOLD = 0.18;

const float HORIZON_FADE = 1.3;

const float MAX_HDR_VALUE = 60000.0;

const float METERS_TO_KILOMETERS = 0.001;

const float CLEARCOAT_F0    = 0.04;
const float MIN_SHEEN_SIN2  = 0.0078125;
const float MIN_SHEEN_ALPHA = 0.0001;

struct surface_data
{
    vec3 albedo;
    vec3 normal;
    vec3 geometric_normal;
    vec3 view;
    vec3 reflected;
    vec3 f0;

    vec2 dfg;

    vec3 diffuse_occlusion;
    vec3 specular_occlusion;

    float metallic;
    float roughness;
    float alpha;
    float n_dot_v;
    float energy_compensation;

    float diffuse_transmission;
    vec3  diffuse_transmission_color;

    float clearcoat;
    float clearcoat_roughness;
    float clearcoat_alpha;
    vec3  clearcoat_normal;
    vec3  clearcoat_reflected;
    float clearcoat_n_dot_v;

    vec3  sheen_color;
    float sheen_roughness;
    float sheen_alpha;
    float sheen_energy;
    float sheen_scaling;
};

float checker_at(vec2 uv)
{
    vec2 cell = floor(uv * CHECKER_CELLS);

    return mod(cell.x + cell.y, 2.0) < 1.0 ? 1.0 : CHECKER_DARK;
}

float max3(vec3 value)
{
    return max(value.r, max(value.g, value.b));
}

vec2 surface_uv;

vec4 sample_material(material_data material, uint texture_index, uint sampler_index, uint slot)
{
    vec2 uv = material_uv(material, slot, surface_uv);

    vec2 uv_dx = material_uv(material, slot, frag_uv + dFdx(frag_uv)) - material_uv(material, slot, frag_uv);
    vec2 uv_dy = material_uv(material, slot, frag_uv + dFdy(frag_uv)) - material_uv(material, slot, frag_uv);

    return textureGrad(
        sampler2D(scene_textures[nonuniformEXT(texture_index)], scene_samplers[nonuniformEXT(sampler_index)]),
        uv,
        uv_dx,
        uv_dy
    );
}

float height_at(material_data material, vec2 uv, vec2 uv_dx, vec2 uv_dy)
{
    return textureGrad(
        sampler2D(scene_textures[nonuniformEXT(material.height_texture)], scene_samplers[nonuniformEXT(material.height_sampler)]),
        material_uv(material, TEXTURE_SLOT_BASE_COLOR, uv),
        uv_dx,
        uv_dy
    ).r;
}

vec2 parallax_uv(material_data material)
{
    if (material.height_texture == NO_TEXTURE || material.height_scale <= 0.0) {
        return frag_uv;
    }

    vec3 normal             = normalize(frag_normal);
    vec3 orthogonal_tangent = frag_tangent.xyz - normal * dot(normal, frag_tangent.xyz);

    if (frag_tangent.w == 0.0 || dot(orthogonal_tangent, orthogonal_tangent) <= MIN_TANGENT_LENGTH2) {
        return frag_uv;
    }

    vec3 tangent   = normalize(orthogonal_tangent);
    vec3 bitangent = cross(normal, tangent) * frag_tangent.w;

    vec3 view = normalize(pc.frame.camera_pos - frag_world_pos);

    vec3 view_tangent = vec3(dot(view, tangent), dot(view, bitangent), dot(view, normal));

    if (view_tangent.z <= 0.0) {
        return frag_uv;
    }

    vec2 uv_dx = material_uv(material, TEXTURE_SLOT_BASE_COLOR, frag_uv + dFdx(frag_uv)) - material_uv(material, TEXTURE_SLOT_BASE_COLOR, frag_uv);
    vec2 uv_dy = material_uv(material, TEXTURE_SLOT_BASE_COLOR, frag_uv + dFdy(frag_uv)) - material_uv(material, TEXTURE_SLOT_BASE_COLOR, frag_uv);

    float layer_count = mix(PARALLAX_MAX_LAYERS, PARALLAX_MIN_LAYERS, view_tangent.z);
    float layer_depth = 1.0 / layer_count;

    vec2 shift = view_tangent.xy / max(view_tangent.z, PARALLAX_MIN_VIEW_Z) * material.height_scale * layer_depth;

    vec2 uv = frag_uv;

    float current_depth = 0.0;
    float sampled_depth = 1.0 - height_at(material, uv, uv_dx, uv_dy);

    for (float layer = 0.0; layer < layer_count && current_depth < sampled_depth; layer += 1.0) {
        uv            -= shift;
        current_depth += layer_depth;
        sampled_depth  = 1.0 - height_at(material, uv, uv_dx, uv_dy);
    }

    vec2 previous_uv = uv + shift;

    float after  = sampled_depth - current_depth;
    float before = 1.0 - height_at(material, previous_uv, uv_dx, uv_dy) - (current_depth - layer_depth);

    float weight = abs(after - before) > PARALLAX_EPSILON ? after / (after - before) : 0.0;

    return mix(uv, previous_uv, clamp(weight, 0.0, 1.0));
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

float visibility_kelemen(float l_dot_h)
{
    return 0.25 / max(l_dot_h * l_dot_h, 0.0001);
}

float distribution_charlie(float alpha, float n_dot_h)
{
    float inv_alpha = 1.0 / alpha;
    float sin2h     = max(1.0 - n_dot_h * n_dot_h, MIN_SHEEN_SIN2);

    return (2.0 + inv_alpha) * pow(sin2h, inv_alpha * 0.5) / (2.0 * PI);
}

float visibility_neubelt(float n_dot_v, float n_dot_l)
{
    return 1.0 / max(4.0 * (n_dot_l + n_dot_v - n_dot_l * n_dot_v), 0.0001);
}

vec3 fresnel_schlick(float cos_theta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

vec3 fresnel_schlick_roughness(float cos_theta, vec3 f0, float roughness)
{
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

float clearcoat_fresnel(float cos_theta, float clearcoat)
{
    return (CLEARCOAT_F0 + (1.0 - CLEARCOAT_F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0)) * clearcoat;
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

vec3 multi_bounce_occlusion(float occlusion, vec3 albedo)
{
    vec3 a =  2.0404 * albedo - 0.3324;
    vec3 b = -4.7951 * albedo + 0.6417;
    vec3 c =  2.7552 * albedo + 0.6903;

    return max(vec3(occlusion), ((occlusion * a + b) * occlusion + c) * occlusion);
}

float burley_fresnel(float cos_theta, float f90)
{
    return 1.0 + (f90 - 1.0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

float burley_diffuse(float n_dot_l, float n_dot_v, float l_dot_h, float roughness)
{
    float f90 = 0.5 + 2.0 * roughness * l_dot_h * l_dot_h;

    return burley_fresnel(n_dot_l, f90) * burley_fresnel(n_dot_v, f90);
}

float horizon_occlusion(vec3 reflected, vec3 geometric_normal)
{
    float horizon = clamp(1.0 + HORIZON_FADE * dot(reflected, geometric_normal), 0.0, 1.0);

    return horizon * horizon;
}

vec3 disk_specular_direction(surface_data surface, light_data light, vec3 to_light)
{
    float d = dot(to_light, surface.reflected);

    if (d >= light.cos_angular_radius) {
        return surface.reflected;
    }

    vec3 tangent = surface.reflected - d * to_light;

    if (dot(tangent, tangent) <= MIN_TANGENT_LENGTH2) {
        return to_light;
    }

    return normalize(to_light * light.cos_angular_radius + normalize(tangent) * light.sin_angular_radius);
}

vec3 apply_normal_map(material_data material, vec3 normal, uint texture_index, uint sampler_index, uint slot, float scale)
{
    vec3 orthogonal_tangent = frag_tangent.xyz - normal * dot(normal, frag_tangent.xyz);

    if (texture_index == NO_TEXTURE || frag_tangent.w == 0.0 || dot(orthogonal_tangent, orthogonal_tangent) <= MIN_TANGENT_LENGTH2) {
        return normal;
    }

    vec3 tangent   = normalize(orthogonal_tangent);
    vec3 bitangent = cross(normal, tangent) * frag_tangent.w;

    vec3 sampled = sample_material(material, texture_index, sampler_index, slot).xyz * 2.0 - 1.0;

    sampled.xy *= scale;

    return normalize(mat3(tangent, bitangent, normal) * sampled);
}

vec3 base_layer(surface_data surface, vec3 to_light, vec3 specular_to_light, float n_dot_l)
{
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

    float l_dot_h = clamp(dot(to_light, normalize(surface.view + to_light)), 0.0, 1.0);

    vec3 diffuse = (1.0 - fresnel) * (1.0 - surface.metallic) * surface.albedo / PI
                 * burley_diffuse(n_dot_l, surface.n_dot_v, l_dot_h, surface.roughness)
                 * (1.0 - surface.diffuse_transmission)
                 * n_dot_l;

    return diffuse + specular;
}

vec3 sheen_layer(surface_data surface, vec3 to_light, float n_dot_l)
{
    vec3 half_vector = normalize(surface.view + to_light);

    float n_dot_h = clamp(dot(surface.normal, half_vector), 0.0, 1.0);

    return surface.sheen_color
         * distribution_charlie(surface.sheen_alpha, n_dot_h)
         * visibility_neubelt(surface.n_dot_v, n_dot_l)
         * n_dot_l;
}

vec3 brdf(surface_data surface, vec3 to_light, vec3 specular_to_light)
{
    float n_dot_l_signed = dot(surface.normal, to_light);
    float n_dot_l        = clamp(n_dot_l_signed, 0.0, 1.0);

    vec3 transmitted = surface.diffuse_transmission * surface.diffuse_transmission_color
                     * (1.0 - surface.metallic) * surface.albedo / PI
                     * clamp(-n_dot_l_signed, 0.0, 1.0);

    if (n_dot_l <= 0.0) {
        return transmitted;
    }

    vec3 color = base_layer(surface, to_light, specular_to_light, n_dot_l);

    if (surface.sheen_scaling < 1.0) {
        color = color * surface.sheen_scaling + sheen_layer(surface, to_light, n_dot_l);
    }

    if (surface.clearcoat > 0.0) {
        vec3 half_vector = normalize(surface.view + to_light);

        float n_dot_h = clamp(dot(surface.clearcoat_normal, half_vector), 0.0, 1.0);
        float l_dot_h = clamp(dot(to_light, half_vector), 0.0, 1.0);

        float clearcoat_n_dot_l = clamp(dot(surface.clearcoat_normal, to_light), 0.0, 1.0);

        float fresnel = clearcoat_fresnel(l_dot_h, surface.clearcoat);

        float specular = distribution_ggx(n_dot_h, surface.clearcoat_alpha) * visibility_kelemen(l_dot_h) * fresnel;

        color = color * (1.0 - fresnel) + vec3(specular * clearcoat_n_dot_l);
    }

    return color + transmitted;
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

    vec3 diffuse  = (1.0 - fresnel) * (1.0 - surface.metallic) * surface.albedo * irradiance * surface.diffuse_occlusion;
    vec3 specular = fresnel * reflection * surface.specular_occlusion;

    return diffuse + specular;
}

vec3 environment_irradiance(vec3 n)
{
    environment_sh_buffer sh = pc.frame.environment_sh;

    vec4 result = sh.coefficients[0] * 0.282095
                + sh.coefficients[1] * (0.488603 * n.y)
                + sh.coefficients[2] * (0.488603 * n.z)
                + sh.coefficients[3] * (0.488603 * n.x)
                + sh.coefficients[4] * (1.092548 * n.x * n.y)
                + sh.coefficients[5] * (1.092548 * n.y * n.z)
                + sh.coefficients[6] * (0.315392 * (3.0 * n.z * n.z - 1.0))
                + sh.coefficients[7] * (1.092548 * n.x * n.z)
                + sh.coefficients[8] * (0.546274 * (n.x * n.x - n.y * n.y));

    return max(result.rgb, vec3(0.0));
}

vec3 environment_radiance(vec3 direction, float roughness)
{
    return textureLod(env_prefiltered, direction, roughness * PREFILTERED_MAX_LOD).rgb;
}

vec3 environment_contribution(surface_data surface)
{
    vec3 irradiance = environment_irradiance(surface.normal) * (1.0 - surface.diffuse_transmission)
                    + environment_irradiance(-surface.normal) * surface.diffuse_transmission * surface.diffuse_transmission_color;

    vec3 radiance = environment_radiance(surface.reflected, surface.roughness);

    vec3 fresnel = fresnel_schlick_roughness(surface.n_dot_v, surface.f0, surface.roughness);

    vec3 single_scatter = fresnel * surface.dfg.x + surface.dfg.y;

    float single_energy = surface.dfg.x + surface.dfg.y;
    float multi_energy  = 1.0 - single_energy;

    vec3 average_fresnel = surface.f0 + (1.0 - surface.f0) / 21.0;
    vec3 multi_scatter   = single_scatter * average_fresnel / (1.0 - multi_energy * average_fresnel);

    vec3 diffuse_color = surface.albedo * (1.0 - surface.metallic);
    vec3 diffuse       = diffuse_color * (1.0 - single_scatter - multi_scatter * multi_energy);

    vec3 specular = single_scatter * radiance * surface.specular_occlusion;
    vec3 ambient  = (multi_scatter * multi_energy + diffuse) * irradiance * surface.diffuse_occlusion;

    vec3 color = specular + ambient;

    if (surface.sheen_scaling < 1.0) {
        vec3 sheen = surface.sheen_color * surface.sheen_energy
                   * environment_radiance(surface.reflected, surface.sheen_roughness)
                   * surface.specular_occlusion;

        color = color * surface.sheen_scaling + sheen;
    }

    if (surface.clearcoat > 0.0) {
        float clearcoat = clearcoat_fresnel(surface.clearcoat_n_dot_v, surface.clearcoat);

        vec3 clearcoat_radiance = environment_radiance(surface.clearcoat_reflected, surface.clearcoat_roughness);

        color = color * (1.0 - clearcoat) + clearcoat_radiance * clearcoat * surface.specular_occlusion;
    }

    return color * pc.frame.environment_intensity;
}

vec3 light_contribution(light_data light, surface_data surface)
{
    if (light.type == LIGHT_TYPE_DIRECTIONAL) {
        vec3 to_light = -light.direction;

        return light.color * brdf(surface, to_light, disk_specular_direction(surface, light, to_light));
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

    surface_uv = parallax_uv(material);

    vec3 surface = vec3(checker_at(frag_uv));
    float alpha  = material.base_color.a;

    if (material.base_color_texture != NO_TEXTURE) {
        vec4 sampled = sample_material(material, material.base_color_texture, material.base_color_sampler, TEXTURE_SLOT_BASE_COLOR);

        surface = sampled.rgb;
        alpha  *= sampled.a;
    }

    if (material.alpha_mode == ALPHA_MODE_MASK && alpha < material.alpha_cutoff) {
        discard;
    }

    float metallic  = material.metallic;
    float roughness = material.roughness;

    if (material.metallic_roughness_texture != NO_TEXTURE) {
        vec4 sampled = sample_material(material, material.metallic_roughness_texture, material.metallic_roughness_sampler, TEXTURE_SLOT_METALLIC_ROUGHNESS);

        metallic  *= sampled.b;
        roughness *= sampled.g;
    }

    float occlusion = texelFetch(scene_gtao, ivec2(gl_FragCoord.xy), 0).r;

    if (material.occlusion_texture != NO_TEXTURE) {
        float sampled = sample_material(material, material.occlusion_texture, material.occlusion_sampler, TEXTURE_SLOT_OCCLUSION).r;

        occlusion *= 1.0 + material.occlusion_strength * (sampled - 1.0);
    }

    vec3 emissive = material.emissive * material.emissive_strength;

    if (material.emissive_texture != NO_TEXTURE) {
        emissive *= sample_material(material, material.emissive_texture, material.emissive_sampler, TEXTURE_SLOT_EMISSIVE).rgb;
    }

    float specular_factor = material.specular_factor;
    vec3  specular_color  = material.specular_color;

    if (material.specular_texture != NO_TEXTURE) {
        specular_factor *= sample_material(material, material.specular_texture, material.specular_sampler, TEXTURE_SLOT_SPECULAR).a;
    }

    if (material.specular_color_texture != NO_TEXTURE) {
        specular_color *= sample_material(material, material.specular_color_texture, material.specular_color_sampler, TEXTURE_SLOT_SPECULAR_COLOR).rgb;
    }

    float ior_f0 = pow((material.ior - 1.0) / (material.ior + 1.0), 2.0);

    vec3 dielectric_f0 = min(vec3(ior_f0) * specular_color, vec3(1.0)) * specular_factor;

    vec3 geometric_normal = normalize(frag_normal);

    vec3 base_normal      = apply_normal_map(material, geometric_normal, material.normal_texture, material.normal_sampler, TEXTURE_SLOT_NORMAL, material.normal_scale);
    vec3 clearcoat_normal = apply_normal_map(material, geometric_normal, material.clearcoat_normal_texture, material.clearcoat_normal_sampler, TEXTURE_SLOT_CLEARCOAT_NORMAL, material.clearcoat_normal_scale);

    float facing = gl_FrontFacing ? 1.0 : -1.0;

    surface_data data;

    data.albedo           = material.base_color.rgb * frag_color * surface;
    data.normal           = base_normal * facing;
    data.geometric_normal = geometric_normal * facing;
    data.view             = normalize(pc.frame.camera_pos - frag_world_pos);
    data.reflected        = reflect(-data.view, data.normal);

    data.metallic  = clamp(metallic, 0.0, 1.0);
    data.roughness = clamp(specular_anti_aliasing(data.normal, clamp(roughness, 0.0, 1.0)), MIN_ROUGHNESS, 1.0);
    data.alpha     = data.roughness * data.roughness;
    data.n_dot_v   = clamp(dot(data.normal, data.view), MIN_N_DOT_V, 1.0);

    data.f0 = mix(dielectric_f0, data.albedo, data.metallic);

    data.diffuse_occlusion  = multi_bounce_occlusion(occlusion, data.albedo * (1.0 - data.metallic));
    data.specular_occlusion = multi_bounce_occlusion(specular_occlusion(data.n_dot_v, occlusion, data.roughness), data.f0)
                            * horizon_occlusion(data.reflected, data.geometric_normal);

    data.dfg = texture(env_brdf_lut, vec2(data.n_dot_v, data.roughness)).rg;

    data.energy_compensation = 1.0 + max3(data.f0) * (1.0 / max(data.dfg.x + data.dfg.y, 0.0001) - 1.0);

    data.diffuse_transmission       = material.diffuse_transmission_factor;
    data.diffuse_transmission_color = material.diffuse_transmission_color;

    if (material.diffuse_transmission_texture != NO_TEXTURE) {
        data.diffuse_transmission *= sample_material(material, material.diffuse_transmission_texture, material.diffuse_transmission_sampler, TEXTURE_SLOT_DIFFUSE_TRANSMISSION).a;
    }

    if (material.diffuse_transmission_color_texture != NO_TEXTURE) {
        data.diffuse_transmission_color *= sample_material(material, material.diffuse_transmission_color_texture, material.diffuse_transmission_color_sampler, TEXTURE_SLOT_DIFFUSE_TRANSMISSION_COLOR).rgb;
    }

    data.clearcoat           = material.clearcoat_factor;
    data.clearcoat_roughness = material.clearcoat_roughness;

    if (material.clearcoat_texture != NO_TEXTURE) {
        data.clearcoat *= sample_material(material, material.clearcoat_texture, material.clearcoat_sampler, TEXTURE_SLOT_CLEARCOAT).r;
    }

    if (material.clearcoat_roughness_texture != NO_TEXTURE) {
        data.clearcoat_roughness *= sample_material(material, material.clearcoat_roughness_texture, material.clearcoat_roughness_sampler, TEXTURE_SLOT_CLEARCOAT_ROUGHNESS).g;
    }

    data.clearcoat_roughness = clamp(data.clearcoat_roughness, MIN_ROUGHNESS, 1.0);
    data.clearcoat_alpha     = data.clearcoat_roughness * data.clearcoat_roughness;
    data.clearcoat_normal    = clearcoat_normal * facing;
    data.clearcoat_reflected = reflect(-data.view, data.clearcoat_normal);
    data.clearcoat_n_dot_v   = clamp(dot(data.clearcoat_normal, data.view), MIN_N_DOT_V, 1.0);

    data.sheen_color     = material.sheen_color;
    data.sheen_roughness = material.sheen_roughness;

    if (material.sheen_color_texture != NO_TEXTURE) {
        data.sheen_color *= sample_material(material, material.sheen_color_texture, material.sheen_color_sampler, TEXTURE_SLOT_SHEEN_COLOR).rgb;
    }

    if (material.sheen_roughness_texture != NO_TEXTURE) {
        data.sheen_roughness *= sample_material(material, material.sheen_roughness_texture, material.sheen_roughness_sampler, TEXTURE_SLOT_SHEEN_ROUGHNESS).a;
    }

    data.sheen_roughness = clamp(data.sheen_roughness, 0.0, 1.0);
    data.sheen_alpha     = max(data.sheen_roughness * data.sheen_roughness, MIN_SHEEN_ALPHA);
    data.sheen_energy    = texture(env_brdf_lut, vec2(data.n_dot_v, data.sheen_roughness)).b;
    data.sheen_scaling   = 1.0 - max3(data.sheen_color) * data.sheen_energy;

    vec3 lighting = vec3(0.0);

    if (pc.frame.has_environment != 0u) {
        lighting += environment_contribution(data);
    }

    for (uint i = 0u; i < pc.frame.light_count; i++) {
        lighting += light_contribution(pc.frame.lights.data[i], data);
    }

    vec3 color = lighting + emissive;

    if (pc.frame.has_atmosphere != 0u) {
        atmosphere_params atmosphere = pc.frame.atmosphere.params;

        float distance = length(frag_world_pos - pc.frame.camera_pos) * METERS_TO_KILOMETERS;

        vec3 coord = vec3(gl_FragCoord.xy / vec2(textureSize(scene_gtao, 0)), clamp(distance / atmosphere.aerial_max_distance, 0.0, 1.0));

        vec4 aerial      = texture(env_aerial_perspective, coord);
        vec3 moon_aerial = texture(env_moon_aerial_perspective, coord).rgb;

        color = color * aerial.a + aerial.rgb * atmosphere.sun_illuminance + moon_aerial * atmosphere.moon_color * atmosphere.moon_illuminance;

        vec3 fog_ambient = pc.frame.environment_sh.coefficients[0].rgb * FOG_SH_BAND0 * pc.frame.environment_intensity;

        vec4 fog = height_fog(atmosphere, pc.frame.camera_pos, frag_world_pos, fog_ambient);

        color = color * fog.a + fog.rgb;
    }

    out_color = vec4(min(color * pc.frame.exposure, vec3(MAX_HDR_VALUE)), 1.0);
}
