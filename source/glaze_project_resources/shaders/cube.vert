#version 450

layout (location = 0) out vec3 fragColor;
layout (location = 1) out vec3 fragNormal;

struct face
{
    vec4 color;
    uvec4 rng;
};

layout (std430, set = 0, binding = 0) readonly buffer pos_buffer
{
    vec4 pos[];
};

layout (std430, set = 0, binding = 1) readonly buffer face_buffer
{
    face faces[];
};

layout (std430, set = 0, binding = 2) readonly buffer normal_buffer
{
    vec4 normals[];
};

layout (push_constant) uniform constants
{
    mat4 mvp;
} pc;

void main ()
{
    uint face_index = gl_VertexIndex / 6;

    gl_Position = pc.mvp * pos[gl_VertexIndex];
    fragColor   = faces[face_index].color.rgb;
    fragNormal  = normals[face_index].xyz;
}
