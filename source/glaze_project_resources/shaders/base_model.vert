#version 450
#extension GL_EXT_buffer_reference : require

#include "util/scene_head.glsl"

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec2 frag_uv;

layout(location = 3) flat out uint frag_texture_index;
layout(location = 4) flat out uint frag_sampler_index;

void main()
{
    object_data object = pc.frame.objects.data[gl_InstanceIndex];

    vertex v = object.vertices.data[gl_VertexIndex];

    gl_Position = pc.frame.view_proj * object.model * vec4(v.pos, 1.0);

    frag_color  = v.color.rgb;
    frag_normal = mat3(object.model) * v.normal;
    frag_uv     = vec2(v.uv_x, v.uv_y);

    frag_texture_index = object.texture_index;
    frag_sampler_index = object.sampler_index;
}
