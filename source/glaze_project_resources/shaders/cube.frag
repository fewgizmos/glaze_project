#version 450

layout (location = 0) in vec3 fragColor;
layout (location = 1) in vec3 fragNormal;

layout (location = 0) out vec4 outColor;

const vec3  LIGHT_DIR = normalize (vec3 (0.5, 1.0, 0.8));
const float AMBIENT   = 0.2;

void main ()
{
    float diffuse = max (dot (normalize (fragNormal), LIGHT_DIR), 0.0);

    outColor = vec4 (fragColor * (AMBIENT + diffuse * (1.0 - AMBIENT)), 1.0);
}
