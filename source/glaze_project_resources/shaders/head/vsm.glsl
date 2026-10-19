const float VSM_MAX_BLOCKER_DISTANCE = 30.0;
const float VSM_MAX_SEARCH_TEXELS    = 60.0;
const int   VSM_WINDOW_MARGIN        = 1;

vec3 vsm_light_space(vsm_params_buffer params, vec3 world_pos)
{
    vec3 relative = world_pos - params.camera_pos;

    return vec3(
        dot(relative, params.light_x.xyz),
        dot(relative, params.light_y.xyz),
        dot(world_pos, params.light_z.xyz)
    );
}

float vsm_texel_size(vsm_params_buffer params, uint level)
{
    return params.levels[level].page_world / float(VSM_PAGE_SIZE);
}

float vsm_search_radius(vsm_params_buffer params, uint level)
{
    float texel = vsm_texel_size(params, level);

    return clamp(VSM_MAX_BLOCKER_DISTANCE * params.tan_angular_radius * params.softness, texel, texel * VSM_MAX_SEARCH_TEXELS);
}

vec2 vsm_window_coord(vsm_params_buffer params, uint level, vec2 light_xy)
{
    vsm_level target = params.levels[level];

    return (light_xy - target.origin_offset) / target.page_world;
}

bool vsm_inside_window(vsm_params_buffer params, uint level, vec2 light_xy)
{
    ivec2 relative = ivec2(floor(vsm_window_coord(params, level, light_xy)));

    return all(greaterThanEqual(relative, ivec2(VSM_WINDOW_MARGIN))) && all(lessThan(relative, ivec2(int(VSM_LEVEL_PAGES) - VSM_WINDOW_MARGIN)));
}

const float VSM_MIN_LEVEL = 4.0;

uint vsm_select_level(vsm_params_buffer params, float distance, vec2 light_xy)
{
    float footprint = max(distance * params.pixel_angle, 0.000001);

    float level = max(floor(log2(footprint / params.texel0) + params.lod_bias), VSM_MIN_LEVEL);

    uint result = uint(clamp(level, 0.0, float(VSM_LEVELS - 1u)));

    while (result < VSM_LEVELS - 1u && !vsm_inside_window(params, result, light_xy)) {
        result++;
    }

    return result;
}

uint vsm_slot(uint level, ivec2 page)
{
    return level * VSM_LEVEL_SLOTS + uint(page.y & int(VSM_LEVEL_PAGES - 1u)) * VSM_LEVEL_PAGES + uint(page.x & int(VSM_LEVEL_PAGES - 1u));
}

bool vsm_window_page(vsm_params_buffer params, uint level, vec2 light_xy, out ivec2 page, out vec2 in_page)
{
    vec2  coord    = vsm_window_coord(params, level, light_xy);
    ivec2 relative = ivec2(floor(coord));

    if (any(lessThan(relative, ivec2(0))) || any(greaterThanEqual(relative, ivec2(VSM_LEVEL_PAGES)))) {
        return false;
    }

    page    = params.levels[level].origin + relative;
    in_page = coord - vec2(relative);

    return true;
}

bool vsm_find_texel(vsm_params_buffer params, uint level, vec2 light_xy, out ivec2 texel, out uint entry_state)
{
    ivec2 page;
    vec2  in_page;

    if (!vsm_window_page(params, level, light_xy, page, in_page)) {
        return false;
    }

    vsm_entry entry = params.entries.data[vsm_slot(level, page)];

    if (entry.tag_x != page.x || entry.tag_y != page.y || entry.physical == VSM_NO_PAGE) {
        return false;
    }

    ivec2 physical = ivec2(entry.physical % VSM_POOL_PAGES, entry.physical / VSM_POOL_PAGES);

    texel       = physical * int(VSM_PAGE_SIZE) + min(ivec2(in_page * float(VSM_PAGE_SIZE)), ivec2(VSM_PAGE_SIZE - 1u));
    entry_state = entry.state;

    return true;
}

float vsm_depth_to_light(vsm_params_buffer params, uint level, float depth)
{
    return (depth - 0.5) / params.levels[level].depth_scale;
}
