const float CLOUD_EXTINCTION = 40.0;

const float SHAPE_FREQUENCY   = 0.2;
const float DETAIL_FREQUENCY  = 2.0;
const float WEATHER_FREQUENCY = 0.025;

const float TYPE_VARIATION = 0.4;
const float DETAIL_EROSION     = 0.35;
const float DETAIL_HEIGHT_FLIP = 10.0;
const float EDGE_EROSION_SCALE = 3.0;

const float BASE_HEIGHT_VARIATION = 0.15;
const float EDGE_HEIGHT_NOISE     = 0.4;

const float MIN_DENSITY = 0.0001;

const vec4 STRATUS_GRADIENT       = vec4(0.0, 0.3, 0.3, 0.7);
const vec4 STRATOCUMULUS_GRADIENT = vec4(0.0, 0.45, 0.5, 1.0);
const vec4 CUMULUS_GRADIENT       = vec4(0.0, 0.5, 0.5, 1.0);

float height_gradient(float height_fraction, float type)
{
    vec4 gradient = type < 0.5
        ? mix(STRATUS_GRADIENT, STRATOCUMULUS_GRADIENT, type * 2.0)
        : mix(STRATOCUMULUS_GRADIENT, CUMULUS_GRADIENT, type * 2.0 - 1.0);

    return smoothstep(gradient.x, gradient.y, height_fraction) - smoothstep(gradient.z, gradient.w, height_fraction);
}

float cloud_base(atmosphere_params params, vec3 position, float height_fraction, out vec3 sample_position, out float shaped_height)
{
    sample_position = vec3(0.0);
    shaped_height   = 0.0;

    if (height_fraction < 0.0 || height_fraction > 1.0) {
        return 0.0;
    }

    vec3 weather = textureLod(weather_map, position.xz * WEATHER_FREQUENCY, 0.0).rgb;

    float coverage = clamp(remap(weather.r, 1.0 - params.cloud_coverage, 1.0, 0.0, 1.0), 0.0, 1.0);
    float type     = clamp(params.cloud_type + (weather.g - 0.5) * TYPE_VARIATION, 0.0, 1.0);

    if (coverage <= MIN_DENSITY) {
        return 0.0;
    }

    sample_position = vec3(position.x, length(position) - params.bottom_radius, position.z);

    vec4 shape = textureLod(shape_noise, sample_position * SHAPE_FREQUENCY, 0.0);

    float low_frequency = dot(shape.gba, vec3(0.625, 0.25, 0.125));

    float bottom_offset = weather.b * BASE_HEIGHT_VARIATION + low_frequency * EDGE_HEIGHT_NOISE;
    float top_offset    = (1.0 - low_frequency) * EDGE_HEIGHT_NOISE;

    shaped_height = (height_fraction - bottom_offset) / (1.0 - bottom_offset - top_offset);

    if (shaped_height <= 0.0 || shaped_height >= 1.0) {
        return 0.0;
    }

    float base = remap(shape.r, low_frequency - 1.0, 1.0, 0.0, 1.0);

    base *= height_gradient(shaped_height, type);

    return clamp(remap(base, 1.0 - coverage, 1.0, 0.0, 1.0), 0.0, 1.0) * coverage;
}

float cloud_erode(atmosphere_params params, float base, vec3 sample_position, float shaped_height, float height_fraction)
{
    vec3 detail = textureLod(detail_noise, sample_position * DETAIL_FREQUENCY, 0.0).rgb;

    float high_frequency = dot(detail, vec3(0.625, 0.25, 0.125));

    high_frequency = mix(high_frequency, 1.0 - high_frequency, clamp(height_fraction * DETAIL_HEIGHT_FLIP, 0.0, 1.0));

    float edge    = 1.0 - clamp(4.0 * shaped_height * (1.0 - shaped_height), 0.0, 1.0);
    float erosion = DETAIL_EROSION * mix(1.0, EDGE_EROSION_SCALE, edge);

    return clamp(remap(base, high_frequency * erosion, 1.0, 0.0, 1.0), 0.0, 1.0) * params.cloud_density;
}

float cloud_density(atmosphere_params params, vec3 position, float height_fraction, bool detailed)
{
    vec3 sample_position;
    float shaped_height;

    float base = cloud_base(params, position, height_fraction, sample_position, shaped_height);

    if (base <= 0.0) {
        return 0.0;
    }

    return detailed ? cloud_erode(params, base, sample_position, shaped_height, height_fraction) : base * params.cloud_density;
}
