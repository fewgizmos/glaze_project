#version 450
#extension GL_EXT_buffer_reference : require

#include "util/scene_head.glsl"

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec2 frag_uv;

layout(location = 0) out vec4 out_color;

const float AMBIENT = 0.04;

const vec2  CHECKER_CELLS = vec2(16.0, 8.0);
const float CHECKER_DARK  = 0.35;

float checker_at(vec2 uv)
{
    vec2 cell = floor(uv * CHECKER_CELLS);

    return mod(cell.x + cell.y, 2.0) < 1.0 ? 1.0 : CHECKER_DARK;
}

void main()
{
    float diffuse = max(dot(normalize(frag_normal), normalize(pc.frame.light_dir)), 0.0);

    vec3 albedo = frag_color * checker_at(frag_uv);

    out_color = vec4(albedo * (AMBIENT + diffuse * (1.0 - AMBIENT)), 1.0);
}
