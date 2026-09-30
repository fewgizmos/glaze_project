#version 450

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in uint in_face;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec3 frag_normal;

struct face
{
    vec4 color;
    uvec4 rng;
};

layout(std430, set = 0, binding = 0) readonly buffer face_buffer
{
    face faces[];
};

layout(push_constant) uniform constants
{
    mat4 view_proj;
    mat4 model;
} pc;

void main()
{
    gl_Position = pc.view_proj * pc.model * vec4(in_pos, 1.0);

    frag_color  = faces[in_face].color.rgb;
    frag_normal = mat3(pc.model) * in_normal;
}
