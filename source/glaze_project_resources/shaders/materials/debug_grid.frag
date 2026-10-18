#version 450
#extension GL_EXT_buffer_reference : require

#include "head/scene.glsl"

layout(location = 0) in vec3 world_pos;

layout(location = 0) out vec4 out_color;

const float CELL_SIZE                = 0.5;
const float MIN_PIXELS_BETWEEN_CELLS = 2.0;

const float LINE_WIDTH = 4.0;
const float AXIS_WIDTH = 3.0;

const float GRID_OPACITY = 0.6;

const float FADE_DISTANCE = 1280.0;

const vec3 THIN_COLOR   = vec3(0.05);
const vec3 THICK_COLOR  = vec3(0.20);
const vec3 AXIS_X_COLOR = vec3(0.32, 0.01, 0.01);
const vec3 AXIS_Z_COLOR = vec3(0.02, 0.05, 0.52);

float log10(float x) { return log(x) / log(10.0); }

float satf(float x) { return clamp(x, 0.0, 1.0); }
vec2  satv(vec2 x)  { return clamp(x, vec2(0.0), vec2(1.0)); }

float max2(vec2 v) { return max(v.x, v.y); }

float grid_signal(vec2 pos, vec2 dudv, float cell_size)
{
    dudv *= LINE_WIDTH;
    pos += dudv / 2.0;

    vec2 band = satv(mod(pos, cell_size) / dudv);

    return max2(1.0 - abs(band * 2.0 - 1.0));
}

float axis_signal(float coord, float derivative)
{
    return 1.0 - satf(abs(coord) / (derivative * AXIS_WIDTH));
}

void main()
{
    vec2 pos = world_pos.xz;

    vec2 dudv = vec2(
        length(vec2(dFdx(pos.x), dFdy(pos.x))),
        length(vec2(dFdx(pos.y), dFdy(pos.y)))
    );

    float lod_level = max(0.0, log10(length(dudv) * MIN_PIXELS_BETWEEN_CELLS / CELL_SIZE) + 1.0);
    float lod_fade  = fract(lod_level);

    float cell_size0 = CELL_SIZE * pow(10.0, floor(lod_level));
    float cell_size1 = cell_size0 * 10.0;
    float cell_size2 = cell_size1 * 10.0;

    float signal0 = grid_signal(pos, dudv, cell_size0);
    float signal1 = grid_signal(pos, dudv, cell_size1);
    float signal2 = grid_signal(pos, dudv, cell_size2);

    vec3  color = THIN_COLOR;
    float alpha = signal0 * (1.0 - lod_fade);

    if (signal2 > 0.0) {
        color = THICK_COLOR;
        alpha = signal2;
    } else if (signal1 > 0.0) {
        color = mix(THICK_COLOR, THIN_COLOR, lod_fade);
        alpha = signal1;
    }

    float axis_x = axis_signal(pos.y, dudv.y);
    float axis_z = axis_signal(pos.x, dudv.x);

    color = mix(color, AXIS_X_COLOR, axis_x);
    alpha = max(alpha, axis_x);

    color = mix(color, AXIS_Z_COLOR, axis_z);
    alpha = max(alpha, axis_z);

    alpha *= 1.0 - satf(length(world_pos - pc.frame.camera_pos) / FADE_DISTANCE);
    alpha *= GRID_OPACITY;

    if (alpha <= 0.0) {
        discard;
    }

    out_color = vec4(color, alpha);
}
