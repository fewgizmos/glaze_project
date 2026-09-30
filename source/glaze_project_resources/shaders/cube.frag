#version 450

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

const vec3  LIGHT_DIR = normalize(vec3(0.5, 1.0, 0.8));
const float AMBIENT   = 0.2;

void main()
{
    float diffuse = max(dot(normalize(frag_normal), LIGHT_DIR), 0.0);

    out_color = vec4(frag_color * (AMBIENT + diffuse * (1.0 - AMBIENT)), 1.0);
}
