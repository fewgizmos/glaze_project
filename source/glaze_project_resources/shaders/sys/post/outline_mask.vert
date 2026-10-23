#version 450
#extension GL_EXT_buffer_reference : require

#include "head/scene.glsl"

invariant gl_Position;

void main()
{
    object_data object = pc.frame.objects.data[gl_InstanceIndex];

    vertex v = object.vertices.data[gl_VertexIndex];

    vec4 world_pos = object.model * vec4(v.pos, 1.0);

    gl_Position = pc.frame.unjittered_view_proj * world_pos;
}
