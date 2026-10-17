#ifndef NOISE_GLSL
#define NOISE_GLSL

uvec3 pcg3d(uvec3 v)
{
    v = v * 1664525u + 1013904223u;

    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;

    v ^= v >> 16u;

    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;

    return v;
}

vec3 hash33(ivec3 cell)
{
    return vec3(pcg3d(uvec3(cell))) / float(0xffffffffu);
}

ivec3 wrap_cell(ivec3 cell, int period)
{
    return ((cell % period) + period) % period;
}

float worley(vec3 position, int period)
{
    ivec3 cell = ivec3(floor(position));
    vec3 local = fract(position);

    float min_distance = 1.0;

    for (int z = -1; z <= 1; z++) {
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                ivec3 offset = ivec3(x, y, z);

                vec3 feature = vec3(offset) + hash33(wrap_cell(cell + offset, period));

                min_distance = min(min_distance, length(feature - local));
            }
        }
    }

    return min_distance;
}

vec3 perlin_gradient(ivec3 cell, int period)
{
    return normalize(hash33(wrap_cell(cell, period)) * 2.0 - 1.0 + vec3(0.0001));
}

float perlin(vec3 position, int period)
{
    ivec3 cell = ivec3(floor(position));
    vec3 local = fract(position);

    vec3 fade = local * local * local * (local * (local * 6.0 - 15.0) + 10.0);

    float n000 = dot(perlin_gradient(cell + ivec3(0, 0, 0), period), local - vec3(0.0, 0.0, 0.0));
    float n100 = dot(perlin_gradient(cell + ivec3(1, 0, 0), period), local - vec3(1.0, 0.0, 0.0));
    float n010 = dot(perlin_gradient(cell + ivec3(0, 1, 0), period), local - vec3(0.0, 1.0, 0.0));
    float n110 = dot(perlin_gradient(cell + ivec3(1, 1, 0), period), local - vec3(1.0, 1.0, 0.0));
    float n001 = dot(perlin_gradient(cell + ivec3(0, 0, 1), period), local - vec3(0.0, 0.0, 1.0));
    float n101 = dot(perlin_gradient(cell + ivec3(1, 0, 1), period), local - vec3(1.0, 0.0, 1.0));
    float n011 = dot(perlin_gradient(cell + ivec3(0, 1, 1), period), local - vec3(0.0, 1.0, 1.0));
    float n111 = dot(perlin_gradient(cell + ivec3(1, 1, 1), period), local - vec3(1.0, 1.0, 1.0));

    float x00 = mix(n000, n100, fade.x);
    float x10 = mix(n010, n110, fade.x);
    float x01 = mix(n001, n101, fade.x);
    float x11 = mix(n011, n111, fade.x);

    return mix(mix(x00, x10, fade.y), mix(x01, x11, fade.y), fade.z);
}

float perlin_fbm(vec3 uvw, int frequency, int octaves)
{
    float result    = 0.0;
    float amplitude = 0.5;
    float total     = 0.0;

    for (int i = 0; i < octaves; i++) {
        int period = frequency << i;

        result += perlin(uvw * float(period), period) * amplitude;
        total  += amplitude;

        amplitude *= 0.5;
    }

    return clamp(result / total * 0.5 + 0.5, 0.0, 1.0);
}

float worley_fbm(vec3 uvw, int frequency)
{
    float octave0 = 1.0 - worley(uvw * float(frequency), frequency);
    float octave1 = 1.0 - worley(uvw * float(frequency * 2), frequency * 2);
    float octave2 = 1.0 - worley(uvw * float(frequency * 4), frequency * 4);

    return octave0 * 0.625 + octave1 * 0.25 + octave2 * 0.125;
}

float remap(float value, float old_min, float old_max, float new_min, float new_max)
{
    return new_min + (value - old_min) / (old_max - old_min) * (new_max - new_min);
}

#endif
