#ifndef GBUFFER_ENCODING_GLSL
#define GBUFFER_ENCODING_GLSL

const uint ENTITY_ID_CHANNEL_BITS = 11u;
const uint ENTITY_ID_CHANNEL_MASK = (1u << ENTITY_ID_CHANNEL_BITS) - 1u;

vec2 sign_not_zero(vec2 value)
{
    return vec2(value.x >= 0.0 ? 1.0 : -1.0, value.y >= 0.0 ? 1.0 : -1.0);
}

vec2 encode_normal(vec3 normal)
{
    normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);

    return normal.z >= 0.0 ? normal.xy : (1.0 - abs(normal.yx)) * sign_not_zero(normal.xy);
}

vec3 decode_normal(vec2 encoded)
{
    vec3 normal = vec3(encoded, 1.0 - abs(encoded.x) - abs(encoded.y));

    if (normal.z < 0.0) {
        normal.xy = (1.0 - abs(normal.yx)) * sign_not_zero(normal.xy);
    }

    return normalize(normal);
}

vec2 encode_entity_id(uint id)
{
    return vec2(float(id & ENTITY_ID_CHANNEL_MASK), float((id >> ENTITY_ID_CHANNEL_BITS) & ENTITY_ID_CHANNEL_MASK));
}

uint decode_entity_id(vec2 encoded)
{
    return uint(encoded.x + 0.5) | (uint(encoded.y + 0.5) << ENTITY_ID_CHANNEL_BITS);
}

#endif
