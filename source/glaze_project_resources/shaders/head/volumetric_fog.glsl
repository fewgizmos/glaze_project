vec4 apply_fog(atmosphere_params atmosphere, vec2 screen_uv, vec3 camera, vec3 point, vec3 ambient)
{
    if (pc.frame.has_fog == 0u) {
        return height_fog(atmosphere, camera, point, ambient);
    }

    fog_params_buffer fog = pc.frame.fog;

    float distance = length(point - camera);

    vec4 volume = textureLod(scene_volumetric_fog, vec3(screen_uv, fog_volume_coord(fog, min(distance, fog.far_distance))), 0.0);

    if (distance <= fog.far_distance) {
        return volume;
    }

    vec4 tail = height_fog(atmosphere, camera + (point - camera) / distance * fog.far_distance, point, ambient);

    return vec4(volume.rgb + tail.rgb * volume.a, volume.a * tail.a);
}
