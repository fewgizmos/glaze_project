#version 450
#extension GL_EXT_buffer_reference : require

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec2 frag_uv;

struct vertex
{
    vec3 pos;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 tangent;
    vec4 color;
};

layout(buffer_reference, std430) readonly buffer vertex_buffer
{
    vertex data[];
};

layout(push_constant) uniform constants
{
    mat4 transform;
    vertex_buffer vertices;
} pc;

void main()
{
    vertex v = pc.vertices.data[gl_VertexIndex];

    gl_Position = pc.transform * vec4(v.pos, 1.0);

    frag_color  = v.color.rgb;
    frag_normal = v.normal;
    frag_uv     = vec2(v.uv_x, v.uv_y);
}
