#include "head/atmosphere_params.glsl"
#include "head/vsm_types.glsl"
#include "head/fog_types.glsl"

struct vertex
{
    vec3 pos;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 tangent;
    vec4 color;
};

layout(buffer_reference, std430) buffer vertex_buffer
{
    vertex data[];
};

layout(buffer_reference, std430) readonly buffer index_buffer
{
    uint data[];
};

const uint NO_TEXTURE = 0xFFFFFFFFu;

const uint ALPHA_MODE_OPAQUE = 0u;
const uint ALPHA_MODE_MASK   = 1u;

struct material_data
{
    vec4 base_color;

    vec3 emissive;
    float metallic;

    float roughness;
    float normal_scale;
    float occlusion_strength;
    float alpha_cutoff;

    uint base_color_texture;
    uint base_color_sampler;

    uint metallic_roughness_texture;
    uint metallic_roughness_sampler;

    uint normal_texture;
    uint normal_sampler;

    uint occlusion_texture;
    uint occlusion_sampler;

    uint emissive_texture;
    uint emissive_sampler;

    uint alpha_mode;

    uint specular_texture;
    uint specular_sampler;

    uint specular_color_texture;
    uint specular_color_sampler;

    uint diffuse_transmission_texture;
    uint diffuse_transmission_sampler;

    uint diffuse_transmission_color_texture;
    uint diffuse_transmission_color_sampler;

    uint clearcoat_texture;
    uint clearcoat_sampler;

    uint clearcoat_roughness_texture;
    uint clearcoat_roughness_sampler;

    uint clearcoat_normal_texture;
    uint clearcoat_normal_sampler;

    uint sheen_color_texture;
    uint sheen_color_sampler;

    uint sheen_roughness_texture;
    uint sheen_roughness_sampler;

    uint height_texture;
    uint height_sampler;
    uint padding0;

    vec4 uv_transforms[28];

    vec3 specular_color;
    float specular_factor;

    float ior;
    float emissive_strength;
    float clearcoat_factor;
    float clearcoat_roughness;

    vec3 diffuse_transmission_color;
    float diffuse_transmission_factor;

    vec3 sheen_color;
    float sheen_roughness;

    float clearcoat_normal_scale;
    float height_scale;
    float height_world;
    float padding1;
};

const uint TEXTURE_SLOT_BASE_COLOR                 = 0u;
const uint TEXTURE_SLOT_METALLIC_ROUGHNESS         = 1u;
const uint TEXTURE_SLOT_NORMAL                     = 2u;
const uint TEXTURE_SLOT_OCCLUSION                  = 3u;
const uint TEXTURE_SLOT_EMISSIVE                   = 4u;
const uint TEXTURE_SLOT_SPECULAR                   = 5u;
const uint TEXTURE_SLOT_SPECULAR_COLOR             = 6u;
const uint TEXTURE_SLOT_DIFFUSE_TRANSMISSION       = 7u;
const uint TEXTURE_SLOT_DIFFUSE_TRANSMISSION_COLOR = 8u;
const uint TEXTURE_SLOT_CLEARCOAT                  = 9u;
const uint TEXTURE_SLOT_CLEARCOAT_ROUGHNESS        = 10u;
const uint TEXTURE_SLOT_CLEARCOAT_NORMAL           = 11u;
const uint TEXTURE_SLOT_SHEEN_COLOR                = 12u;
const uint TEXTURE_SLOT_SHEEN_ROUGHNESS            = 13u;

vec2 material_uv(material_data material, uint slot, vec2 uv)
{
    vec3 source = vec3(uv, 1.0);

    return vec2(dot(material.uv_transforms[slot * 2u].xyz, source), dot(material.uv_transforms[slot * 2u + 1u].xyz, source));
}

layout(buffer_reference, std430) readonly buffer material_buffer
{
    material_data data[];
};

struct object_data
{
    mat4 model;
    mat4 previous_model;
    vertex_buffer vertices;
    uint vertex_count;
    uint material_index;
    index_buffer indices;
    uint index_count;
    uint padding0;
};

layout(buffer_reference, std430) readonly buffer object_buffer
{
    object_data data[];
};

const uint LIGHT_TYPE_DIRECTIONAL = 0u;
const uint LIGHT_TYPE_POINT       = 1u;
const uint LIGHT_TYPE_AMBIENT     = 2u;

struct light_data
{
    vec3 position;
    float range;

    vec3 direction;
    float cos_outer;

    vec3 color;
    uint type;

    vec3 ground_color;
    float cos_inner;

    float cos_angular_radius;
    float sin_angular_radius;
    uint shadowed;
    float padding0;
};

layout(buffer_reference, std430) readonly buffer light_buffer
{
    light_data data[];
};

layout(buffer_reference, std430) readonly buffer environment_sh_buffer
{
    vec4 coefficients[9];
};

layout(buffer_reference, std430) readonly buffer frame_buffer
{
    mat4 view;
    mat4 proj;
    mat4 view_proj;
    mat4 inv_view_proj;

    vec3 camera_pos;
    float timestamp;

    uint light_count;
    uint has_environment;
    float environment_intensity;
    float quality;

    object_buffer objects;
    material_buffer materials;
    light_buffer lights;

    uint frame_index;
    float exposure;

    mat4 unjittered_view_proj;
    mat4 previous_view_proj;

    environment_sh_buffer environment_sh;
    atmosphere_buffer atmosphere;

    uint has_atmosphere;
    uint has_shadows;

    vsm_params_buffer shadows;
    fog_params_buffer fog;

    uint has_fog;
    uint padding1;
};
