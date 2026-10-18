#version 450
#extension GL_EXT_buffer_reference : require

#include "head/scene.glsl"

const vec2 CORNERS[6] = vec2[](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0),
    vec2(0.0, 0.0),
    vec2(1.0, 1.0),
    vec2(0.0, 1.0)
);

const float FAR_DEPTH = 1.0;

void main()
{
    vsm_params_buffer params = pc.frame.shadows;

    uint id       = params.render_pages.data[gl_InstanceIndex];
    uint physical = params.entries.data[id].physical;

    vec2 pixel = (vec2(physical % VSM_POOL_PAGES, physical / VSM_POOL_PAGES) + CORNERS[gl_VertexIndex]) * float(VSM_PAGE_SIZE);

    gl_Position = vec4(pixel / float(VSM_POOL_SIZE) * 2.0 - 1.0, FAR_DEPTH, 1.0);
}
