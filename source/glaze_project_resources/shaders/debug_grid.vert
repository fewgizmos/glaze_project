#version 450
#extension GL_EXT_buffer_reference : require

#include "util/scene.glsl"

const float FADE_DISTANCE = 1280.0;

layout(location = 0) out vec3 world_pos;

const vec2 CORNERS[4] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2(-1.0,  1.0),
    vec2( 1.0,  1.0)
);

void main()
{
    vec2 corner = CORNERS[gl_VertexIndex];

    vec3 center = vec3(pc.frame.camera_pos.x, 0.0, pc.frame.camera_pos.z);

    world_pos = center + vec3(corner.x, 0.0, corner.y) * FADE_DISTANCE;

    gl_Position = pc.frame.view_proj * vec4(world_pos, 1.0);
}
