struct vertex
{
    vec3 pos;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 tangent;
    vec4 color;
};

layout(buffer_reference, std430) buffer vertex_buffer
{
    vertex data[];
};

struct object_data
{
    mat4 model;
    vertex_buffer vertices;
    uint vertex_count;
};

layout(buffer_reference, std430) readonly buffer object_buffer
{
    object_data data[];
};

layout(buffer_reference, std430) readonly buffer frame_buffer
{
    mat4 view;
    mat4 proj;
    mat4 view_proj;
    mat4 inv_view_proj;

    vec3 camera_pos;
    float timestamp;

    vec3 light_dir;

    object_buffer objects;
};

layout(push_constant) uniform constants
{
    frame_buffer frame;
} pc;
