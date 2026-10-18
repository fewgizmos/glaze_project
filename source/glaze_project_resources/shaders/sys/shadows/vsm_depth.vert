#version 450
#extension GL_EXT_buffer_reference : require

#include "head/scene.glsl"
#include "head/vsm.glsl"

layout(location = 0) out vec2 frag_uv;
layout(location = 1) flat out uint frag_material_index;

out gl_PerVertex
{
    vec4 gl_Position;
    float gl_ClipDistance[4];
};

void main()
{
    vsm_params_buffer params = pc.frame.shadows;

    uint draw = uint(gl_InstanceIndex);

    uint object_index = params.draw_info.data[draw * 2u];
    uint slot         = params.draw_info.data[draw * 2u + 1u];

    object_data object = pc.frame.objects.data[object_index];

    vertex v = object.vertices.data[object.indices.data[gl_VertexIndex]];

    vec3 world_pos = (object.model * vec4(v.pos, 1.0)).xyz;
    vec3 light_pos = vsm_light_space(params, world_pos);

    uint level = slot / VSM_LEVEL_SLOTS;

    vsm_level target = params.levels[level];
    vsm_entry entry  = params.entries.data[slot];

    vec2 local    = vsm_window_coord(params, level, light_pos.xy) - vec2(ivec2(entry.tag_x, entry.tag_y) - target.origin);
    vec2 physical = vec2(entry.physical % VSM_POOL_PAGES, entry.physical / VSM_POOL_PAGES);
    vec2 pixel    = (physical + local) * float(VSM_PAGE_SIZE);

    gl_Position = vec4(pixel / float(VSM_POOL_SIZE) * 2.0 - 1.0, light_pos.z * target.depth_scale + 0.5, 1.0);

    gl_ClipDistance[0] = local.x;
    gl_ClipDistance[1] = 1.0 - local.x;
    gl_ClipDistance[2] = local.y;
    gl_ClipDistance[3] = 1.0 - local.y;

    frag_uv             = vec2(v.uv_x, v.uv_y);
    frag_material_index = object.material_index;
}
