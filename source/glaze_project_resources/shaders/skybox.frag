#version 450

layout(set = 1, binding = 0) uniform samplerCube skybox_cubemap;

layout(location = 0) in vec3 view_ray;

layout(location = 0) out vec4 out_color;

void main()
{
    out_color = vec4(texture(skybox_cubemap, normalize(view_ray)).rgb, 1.0);
}
