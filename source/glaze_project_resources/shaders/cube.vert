#version 450
#extension GL_EXT_buffer_reference : require

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

layout(buffer_reference, std430) readonly buffer face_buffer
{
    face data[];
};

layout(buffer_reference, std430) readonly buffer object_buffer
{
    mat4 model;
    face_buffer faces;
};

layout(push_constant) uniform constants
{
    mat4 view_proj;
    object_buffer object;
} pc;

void main()
{
    gl_Position = pc.view_proj * pc.object.model * vec4(in_pos, 1.0);

    frag_color  = pc.object.faces.data[in_face].color.rgb;
    frag_normal = mat3(pc.object.model) * in_normal;
}
