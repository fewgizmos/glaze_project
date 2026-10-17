const float PI = 3.14159265359;

const float MIN_GGX_ALPHA2 = 0.0000001;

vec3 cube_direction(uint face, vec2 uv)
{
    vec2 st = uv * 2.0 - 1.0;

    if (face == 0u) { return normalize(vec3( 1.0, -st.y, -st.x)); }
    if (face == 1u) { return normalize(vec3(-1.0, -st.y,  st.x)); }
    if (face == 2u) { return normalize(vec3( st.x,  1.0,  st.y)); }
    if (face == 3u) { return normalize(vec3( st.x, -1.0, -st.y)); }
    if (face == 4u) { return normalize(vec3( st.x, -st.y,  1.0)); }

    return normalize(vec3(-st.x, -st.y, -1.0));
}

float radical_inverse(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);

    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint index, uint count)
{
    return vec2(float(index) / float(count), radical_inverse(index));
}

vec3 importance_sample_ggx(vec2 xi, vec3 normal, float roughness)
{
    float alpha = roughness * roughness;

    float phi       = 2.0 * PI * xi.x;
    float cos_theta = sqrt(clamp((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y), 0.0, 1.0));
    float sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));

    vec3 half_tangent = vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);

    vec3 up        = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);

    return normalize(tangent * half_tangent.x + bitangent * half_tangent.y + normal * half_tangent.z);
}

float distribution_ggx(float n_dot_h, float roughness)
{
    float alpha  = roughness * roughness;
    float alpha2 = max(alpha * alpha, MIN_GGX_ALPHA2);
    float factor = n_dot_h * n_dot_h * (alpha2 - 1.0) + 1.0;

    return alpha2 / (PI * factor * factor);
}
