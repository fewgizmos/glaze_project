#version 450
#extension GL_EXT_buffer_reference : require

#include "head/scene.glsl"

const float SKY_DEPTH = 0.0000001;

layout(location = 0) out vec3 view_ray;
layout(location = 1) out vec2 screen_uv;

const vec2 CORNERS[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main()
{
    vec2 corner = CORNERS[gl_VertexIndex];

    vec4 near_point = pc.frame.inv_view_proj * vec4(corner, 1.0, 1.0);
    vec4 far_point  = pc.frame.inv_view_proj * vec4(corner, 0.0, 1.0);

    view_ray  = far_point.xyz / far_point.w - near_point.xyz / near_point.w;
    screen_uv = corner * 0.5 + 0.5;

    gl_Position = vec4(corner, SKY_DEPTH, 1.0);
}
