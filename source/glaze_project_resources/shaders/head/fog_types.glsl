const uvec3 FOG_GRID = uvec3(160u, 90u, 128u);

layout(buffer_reference, std430) readonly buffer fog_params_buffer
{
    mat4 inv_view_proj;
    mat4 previous_view_proj;

    vec3 camera_pos;
    float near_distance;

    vec3 previous_camera_pos;
    float far_distance;

    float jitter;
    float history_weight;
    uint reset;
    uint parity;
};

float fog_slice_distance(fog_params_buffer fog, float slice)
{
    return fog.near_distance * pow(fog.far_distance / fog.near_distance, slice / float(FOG_GRID.z));
}

float fog_distance_slice(fog_params_buffer fog, float distance)
{
    return log(max(distance, fog.near_distance) / fog.near_distance) / log(fog.far_distance / fog.near_distance) * float(FOG_GRID.z);
}

float fog_volume_coord(fog_params_buffer fog, float distance)
{
    return (fog_distance_slice(fog, distance) - 0.5) / float(FOG_GRID.z);
}
