#version 450
#extension GL_EXT_buffer_reference : require

layout(location = 0) out vec3 frag_color;

struct point
{
    vec4 pos;
    vec4 color;
};

layout(buffer_reference, std430) readonly buffer point_buffer
{
    point data[];
};

layout(push_constant) uniform constants
{
    mat4 view_proj;
    point_buffer points;
    float size;
    float aspect;
} pc;

const uint  SEGMENTS = 16u;
const float TAU      = 6.28318530718;

void main()
{
    point p = pc.points.data[gl_InstanceIndex];

    uint triangle = uint(gl_VertexIndex) / 3u;
    uint corner   = uint(gl_VertexIndex) % 3u;

    vec2 offset = vec2(0.0);

    if (corner != 0u) {
        float angle = TAU * float(triangle + corner - 1u) / float(SEGMENTS);

        offset = vec2(cos(angle), sin(angle));
    }

    vec4 clip = pc.view_proj * vec4(p.pos.xyz, 1.0);

    clip.xy += offset * vec2(pc.size / pc.aspect, pc.size) * p.pos.w;

    gl_Position = clip;

    frag_color = p.color.rgb;
}
