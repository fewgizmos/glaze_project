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

const uint NO_TEXTURE = 0xFFFFFFFFu;

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
};

layout(buffer_reference, std430) readonly buffer material_buffer
{
    material_data data[];
};

struct object_data
{
    mat4 model;
    vertex_buffer vertices;
    uint vertex_count;
    uint material_index;
};

layout(buffer_reference, std430) readonly buffer object_buffer
{
    object_data data[];
};

layout(buffer_reference, std430) readonly buffer frame_buffer
{
    mat4 view;
    mat4 proj;
    mat4 view_proj;
    mat4 inv_view_proj;

    vec3 camera_pos;
    float timestamp;

    vec3 light_dir;

    object_buffer objects;
    material_buffer materials;
};

layout(push_constant) uniform constants
{
    frame_buffer frame;
} pc;
