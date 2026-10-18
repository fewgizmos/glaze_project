const float CLOUD_SHADOW_EXTENT = 16000.0;
const float CLOUD_SHADOW_SIZE   = 512.0;

const uint CLOUD_SHADOW_SUN  = 0u;
const uint CLOUD_SHADOW_MOON = 1u;

vec2 cloud_shadow_center(vec3 camera_world)
{
    float texel = CLOUD_SHADOW_EXTENT / CLOUD_SHADOW_SIZE;

    return floor(camera_world.xz / texel) * texel;
}

vec2 cloud_shadow_uv(vec3 camera_world, vec3 world_pos)
{
    return (world_pos.xz - cloud_shadow_center(camera_world)) / CLOUD_SHADOW_EXTENT + 0.5;
}
