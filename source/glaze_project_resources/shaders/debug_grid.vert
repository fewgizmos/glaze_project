#version 450
#extension GL_EXT_buffer_reference : require

#include "util/scene.glsl"

layout(location = 0) out vec3 near_point;
layout(location = 1) out vec3 far_point;

const vec2 CORNERS[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

vec3 unproject(vec2 xy, float depth)
{
    vec4 point = pc.frame.inv_view_proj * vec4(xy, depth, 1.0);

    return point.xyz / point.w;
}

void main()
{
    vec2 corner = CORNERS[gl_VertexIndex];

    near_point = unproject(corner, 1.0);
    far_point  = unproject(corner, 0.0);

    gl_Position = vec4(corner, 0.0, 1.0);
}
