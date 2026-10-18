#version 450
#extension GL_EXT_buffer_reference : require

#include "head/scene.glsl"

layout(location = 0) in vec2 frag_uv;
layout(location = 1) flat in uint frag_material_index;

void main()
{
    material_data material = pc.frame.materials.data[frag_material_index];

    float alpha = material.base_color.a;

    if (material.base_color_texture != NO_TEXTURE) {
        alpha *= sample_scene_texture(material.base_color_texture, material.base_color_sampler, material_uv(material, TEXTURE_SLOT_BASE_COLOR, frag_uv)).a;
    }

    if (alpha < material.alpha_cutoff) {
        discard;
    }
}
