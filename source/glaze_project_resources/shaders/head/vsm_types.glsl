const uint VSM_LEVELS       = 16u;
const uint VSM_PAGE_SIZE    = 128u;
const uint VSM_LEVEL_PAGES  = 128u;
const uint VSM_LEVEL_SLOTS  = VSM_LEVEL_PAGES * VSM_LEVEL_PAGES;
const uint VSM_POOL_PAGES   = 64u;
const uint VSM_POOL_SIZE    = VSM_POOL_PAGES * VSM_PAGE_SIZE;
const uint VSM_LEVEL_REQUESTS = 2048u;
const uint VSM_PAGE_BUDGET    = 128u;
const uint VSM_MAX_DRAWS    = 262144u;

const uint VSM_NO_PAGE = 0xFFFFFFFFu;

const uint VSM_STATE_REQUESTED = 1u;
const uint VSM_STATE_INVALID   = 2u;
const uint VSM_STATE_RENDER    = 4u;
const uint VSM_STATE_FLAGS     = 0xFFu;
const uint VSM_STATE_AGE_SHIFT = 8u;

const uint VSM_LIST_OPAQUE = 0u;
const uint VSM_LIST_MASKED = 1u;

const uint VSM_DEBUG_NONE   = 0u;
const uint VSM_DEBUG_LEVELS = 1u;
const uint VSM_DEBUG_PAGES  = 2u;

struct vsm_entry
{
    int tag_x;
    int tag_y;
    uint physical;
    uint state;
};

layout(buffer_reference, std430) buffer vsm_entry_buffer
{
    vsm_entry data[];
};

layout(buffer_reference, std430) buffer vsm_uint_buffer
{
    uint data[];
};

struct vsm_draw_command
{
    uint vertex_count;
    uint instance_count;
    uint first_vertex;
    uint first_instance;
};

layout(buffer_reference, std430) buffer vsm_draw_buffer
{
    vsm_draw_command data[];
};

layout(buffer_reference, std430) buffer vsm_counters_buffer
{
    uint free_count;
    uint request_count;
    uint render_count;
    uint failed_count;
    uint draw_counts[2];
    uint requested_count;
    uint padding0;
};

struct vsm_caster
{
    vec3 bounds_min;
    uint object_index;
    vec3 bounds_max;
    uint list;
    uint first_index;
    uint index_count;
    uint padding0;
    uint padding1;
};

layout(buffer_reference, std430) readonly buffer vsm_caster_buffer
{
    vsm_caster data[];
};

struct vsm_box
{
    vec4 bounds_min;
    vec4 bounds_max;
};

layout(buffer_reference, std430) readonly buffer vsm_box_buffer
{
    vsm_box data[];
};

struct vsm_level
{
    ivec2 origin;
    float page_world;
    float depth_scale;

    vec2 origin_offset;
    vec2 padding0;
};

layout(buffer_reference, std430) readonly buffer vsm_params_buffer
{
    vec4 light_x;
    vec4 light_y;
    vec4 light_z;

    vsm_level levels[VSM_LEVELS];

    mat4 inv_view_proj;

    vec3 camera_pos;
    float pixel_angle;

    float texel0;
    float lod_bias;
    float softness;
    float normal_bias;

    float tan_angular_radius;
    uint debug_mode;
    uint caster_count;
    uint box_count;

    float contact_length;
    uint padding0;
    uint padding1;
    uint padding2;

    vsm_entry_buffer entries;
    vsm_uint_buffer free_stack;
    vsm_uint_buffer requests;
    vsm_uint_buffer render_pages;
    vsm_counters_buffer counters;
    vsm_draw_buffer draws;
    vsm_uint_buffer draw_info;
    vsm_draw_buffer clear_args;
    vsm_caster_buffer casters;
    vsm_box_buffer boxes;
    vsm_uint_buffer level_requests;
};
