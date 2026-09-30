#version 450

layout (location = 0) out vec3 fragColor;

struct vertex
{
    vec4 position;
    vec4 color;
};

layout (std430, set = 0, binding = 0) readonly buffer vertex_buffer
{
    vertex vertices[];
};

void main ()
{
    vertex v = vertices[gl_VertexIndex];

    gl_Position = v.position;
    fragColor   = v.color.rgb;
}
