#version 450

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec2 frag_uv;

layout(location = 0) out vec4 out_color;

const vec3  LIGHT_DIR = normalize(vec3(-0.8, 0.6, 0.3));
const float AMBIENT   = 0.04;

const vec2  CHECKER_CELLS = vec2(16.0, 8.0);
const float CHECKER_DARK  = 0.35;

float checker_at(vec2 uv)
{
    vec2 cell = floor(uv * CHECKER_CELLS);

    return mod(cell.x + cell.y, 2.0) < 1.0 ? 1.0 : CHECKER_DARK;
}

void main()
{
    float diffuse = max(dot(normalize(frag_normal), LIGHT_DIR), 0.0);

    vec3 albedo = frag_color * checker_at(frag_uv);

    out_color = vec4(albedo * (AMBIENT + diffuse * (1.0 - AMBIENT)), 1.0);
}
