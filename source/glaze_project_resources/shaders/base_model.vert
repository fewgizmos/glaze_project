#version 450
#extension GL_EXT_buffer_reference : require

#include "util/scene.glsl"

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec2 frag_uv;
layout(location = 3) out vec3 frag_world_pos;
layout(location = 4) out vec4 frag_tangent;

layout(location = 5) flat out uint frag_material_index;

void main()
{
    object_data object = pc.frame.objects.data[gl_InstanceIndex];

    vertex v = object.vertices.data[gl_VertexIndex];

    vec4 world_pos = object.model * vec4(v.pos, 1.0);

    mat3 normal_matrix = transpose(inverse(mat3(object.model)));

    gl_Position = pc.frame.view_proj * world_pos;

    frag_color     = v.color.rgb;
    frag_normal    = normal_matrix * v.normal;
    frag_uv        = vec2(v.uv_x, v.uv_y);
    frag_world_pos = world_pos.xyz;
    frag_tangent   = vec4(mat3(object.model) * v.tangent.xyz, v.tangent.w);

    frag_material_index = object.material_index;
}
