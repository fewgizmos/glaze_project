#version 450
#extension GL_EXT_buffer_reference : require

#include "util/scene.glsl"

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec2 frag_uv;

layout(location = 3) flat in uint frag_material_index;

layout(location = 0) out vec4 out_color;

const vec2  CHECKER_CELLS = vec2(16.0, 8.0);
const float CHECKER_DARK  = 0.35;

float checker_at(vec2 uv)
{
    vec2 cell = floor(uv * CHECKER_CELLS);

    return mod(cell.x + cell.y, 2.0) < 1.0 ? 1.0 : CHECKER_DARK;
}

void main()
{
    material_data material = pc.frame.materials.data[frag_material_index];

    vec3 surface = vec3(checker_at(frag_uv));

    if (material.base_color_texture != NO_TEXTURE) {
        surface = sample_scene_texture(material.base_color_texture, material.base_color_sampler, frag_uv).rgb;
    }

    out_color = vec4(material.base_color.rgb * frag_color * surface, 1.0);
}
